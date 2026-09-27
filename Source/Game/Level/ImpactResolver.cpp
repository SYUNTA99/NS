#include "Game/Level/ImpactResolver.h"

#include "Game/Level/Breakable.h"
#include "Game/Level/ColliderBounds.h"
#include "Game/Level/CollisionInput.h"
#include "Game/Level/ImpactMark.h"
#include "Game/Level/LaunchedBody.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/CameraBrain.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Physics/PhysicsScene.h"
#include "Runtime/Platform/Clock.h"
#include "Runtime/Platform/Input.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace NS::Game::Level
{
    namespace
    {
        // 揺れのフレーム数の上限。揺れの並びは始める時に全フレームぶんの領域を取るので、欄の打ち間違いの大きな値を止める
        // 1 秒を超える揺れは当たりの返りではなく、画面が揺れ続けている状態
        constexpr int k_MaxShakeFrames = 60;

        // 丸めた整数を 32 ビットの符号なしへ写す。負の値も同じ値なら同じビットになる
        [[nodiscard]] std::uint32_t RoundedBits(float value) noexcept
        {
            return static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(value)));
        }

        // 揺れの並びの種。相手の番号・横ずれ (1/1000)・相手の飛ぶ水平の角度 (0.1 度)・相手の位置 (1 cm) を丸めて畳む
        // 丸めた整数だけを混ぜるので、浮動小数の最後の桁の違いでは種が変わらない
        // seed_seq の畳み方は規格で決まっていて、実装によらず同じ値から同じ種を出す
        [[nodiscard]] std::uint32_t ShakeSeed(std::uint32_t targetId,
                                              float offset01,
                                              const NS::Core::Vector3& impactDir,
                                              const NS::Core::Vector3& targetPos)
        {
            const float headingDegrees = NS::Core::RadiansToDegrees(std::atan2(impactDir.z, impactDir.x));
            std::seed_seq values{
                targetId,
                RoundedBits(offset01 * 1000.0f),
                RoundedBits(headingDegrees * 10.0f),
                RoundedBits(targetPos.x * 100.0f),
                RoundedBits(targetPos.y * 100.0f),
                RoundedBits(targetPos.z * 100.0f),
            };
            std::array<std::uint32_t, 1> seed{};
            values.generate(seed.begin(), seed.end());
            return seed[0];
        }

        // 止めのフレーム数 × ratio を切り上げたフレーム数。ratio は 0〜1 に収め、非数は 0 として扱う
        [[nodiscard]] int PullBackFrames(int stopSteps, float ratio) noexcept
        {
            if (!(ratio > 0.0f))
            {
                return 0;
            }
            const float clamped = std::min(ratio, 1.0f);
            return static_cast<int>(std::ceil(static_cast<float>(stopSteps) * clamped));
        }

        // 自機の位置から向きの線を引いた時の、相手の外接箱の中心の測り
        struct LineOffset
        {
            float along = 0.0f; // 自機の位置から相手の中心までの、線に沿った水平の距離 (m)。後ろは負
            float ratio = 0.0f; // 線から相手の中心までの横ずれ ÷ (相手の半幅 + 自機の半径)。0〜1 へ丸めない
        };

        // 当たりの裁定と突進の線の予測が同じ式を通る。式を 2 つ置くと、予測した横ずれと当たりの段が食い違う
        // 分母は AABB を向きに直交する軸へ投影した半幅に自機の半径を足した値
        // 触れられる横ずれの上限が比 1 になる。斜めの箱でも角をかすめる当たりが 1
        // 球と傾いた箱は外接箱で測るので実際の縁より広く出る
        // 事前条件: direction の水平の長さが 0 でない
        [[nodiscard]] LineOffset MeasureLineOffset(const NS::Core::Vector3& position,
                                                   const NS::Core::AABB& bounds,
                                                   const NS::Core::Vector3& direction,
                                                   float playerRadius) noexcept
        {
            const float toX = bounds.Center.x - position.x;
            const float toZ = bounds.Center.z - position.z;
            const float invSpeed = 1.0f / std::sqrt(direction.x * direction.x + direction.z * direction.z);
            const float dirX = direction.x * invSpeed;
            const float dirZ = direction.z * invSpeed;
            const float along = toX * dirX + toZ * dirZ;
            const float lateralX = toX - along * dirX;
            const float lateralZ = toZ - along * dirZ;
            const float lateral = std::sqrt(lateralX * lateralX + lateralZ * lateralZ);
            const float halfWidth = std::abs(dirZ) * bounds.Extents.x + std::abs(dirX) * bounds.Extents.z;
            const float reach = halfWidth + playerRadius;
            // 半幅と半径の和が 0 以下では割れない。中心扱いへ倒す
            if (!(reach > 0.0f))
            {
                return LineOffset{.along = along, .ratio = 0.0f};
            }
            return LineOffset{.along = along, .ratio = lateral / reach};
        }

        // 相手の中心からの横ずれ 0..1。OnUpdate へ式を埋めると当たり判定の流れが読めなくなる
        // 水平が 0 の枝は要らない。向かっていないフレームは内積の判定で先に返しており、水平が 0 のフレームもそこへ入る
        [[nodiscard]] float HitOffset01(const NS::Core::Vector3& position,
                                        const NS::Core::AABB& bounds,
                                        const NS::Core::Vector3& velocity,
                                        float playerRadius) noexcept
        {
            return NS::Core::Clamp(MeasureLineOffset(position, bounds, velocity, playerRadius).ratio, 0.0f, 1.0f);
        }

        [[nodiscard]] JPH::BodyID CurrentBodyOf(const NS::Obj::GameObject& object) noexcept
        {
            // RigidBody の形になった collider は RigidBody の body を返す。飛んでいても置かれていても同じ口で引ける
            if (const NS::Obj::Collider* collider = object.FindComponent<NS::Obj::Collider>())
            {
                return collider->BodyId();
            }
            return JPH::BodyID{};
        }

        // 押し飛ばしの重さ。RigidBody が無い配置物は質量 1 として扱う
        [[nodiscard]] float MassOf(const NS::Obj::GameObject& object) noexcept
        {
            if (const NS::Obj::RigidBody* rigidBody = object.FindComponent<NS::Obj::RigidBody>())
            {
                return rigidBody->EffectiveMass();
            }
            return 1.0f;
        }

        [[nodiscard]] bool IsTouching(const std::vector<JPH::BodyID>& touching, JPH::BodyID id)
        {
            return std::find(touching.begin(), touching.end(), id) != touching.end();
        }

        // 触れる所を詰める幅の下限 (m)。1 mm は地面の矢印の先の位置の違いとして見分けられない長さ
        constexpr float k_ContactTolerance = 0.001f;
        // 触れる所を詰める回数の上限。10 m の線を 1 mm まで詰めるのは 14 回。浮動小数の桁が尽きて幅が縮まない時に止める
        constexpr int k_ContactSearchSteps = 32;

        // 半径 radius の玉が origin から direction へ distance 進む間に通る所。玉を線分に沿って掃いた形はカプセル
        // 事前条件: direction が正規化済み、distance が 0 以上で有限
        [[nodiscard]] NS::Phys::Capsule SweptBall(const NS::Core::Vector3& origin,
                                                  const NS::Core::Vector3& direction,
                                                  float distance,
                                                  float radius) noexcept
        {
            const float half = distance * 0.5f;
            return NS::Phys::Capsule{.center = origin + direction * half,
                                     .axis = direction,
                                     .halfHeight = half,
                                     .radius = radius};
        }

        // 玉が body に初めて触れるまでに線に沿って進む距離 (m)。k_ContactTolerance の幅で、触れている側の端を返す
        // 掃く長さを伸ばすほど触れる body は増えるだけなので、触れない長さと触れる長さの間を半分ずつ詰める
        // 事前条件: SweptBall(origin, direction, distance, radius) が body に触れている
        [[nodiscard]] float FirstTouchDistance(const NS::Phys::PhysicsScene& physics,
                                               const NS::Core::Vector3& origin,
                                               const NS::Core::Vector3& direction,
                                               float distance,
                                               float radius,
                                               JPH::BodyID body)
        {
            if (IsTouching(physics.OverlapCapsule(SweptBall(origin, direction, 0.0f, radius)), body))
            {
                return 0.0f;
            }
            float missed = 0.0f;
            float touched = distance;
            for (int i = 0; i < k_ContactSearchSteps && touched - missed > k_ContactTolerance; ++i)
            {
                const float middle = (missed + touched) * 0.5f;
                if (IsTouching(physics.OverlapCapsule(SweptBall(origin, direction, middle, radius)), body))
                {
                    touched = middle;
                }
                else
                {
                    missed = middle;
                }
            }
            return touched;
        }

        // 体当たりの相手になれる壊せる物なら外接箱を取って true。当たりの裁定と寄せる相手の探索が同じ絞りを通る
        [[nodiscard]] bool TryGetTargetBounds(const Breakable& breakable, NS::Core::AABB& outBounds) noexcept
        {
            if (!breakable.IsActive())
            {
                return false;
            }

            // トリガの箱は通り抜ける体積なのでぶつかる相手にならない
            const NS::Obj::BoxCollider* box = breakable.Owner()->FindComponent<NS::Obj::BoxCollider>();
            if (box != nullptr && box->IsTrigger())
            {
                return false;
            }

            return TryGetColliderBounds(*breakable.Owner(), outBounds);
        }
    } // namespace

    // PlayerComponent の 200 より前。書き込んだ速度が同じ固定ステップの移動に乗る
    ImpactResolver::ImpactResolver() noexcept : NS::Obj::OverlayRenderer(NS::Obj::TickPriority::Update - 100) {}

    void ImpactResolver::OnStart()
    {
        // 基底が重ね描きの登録簿へ自分を入れる
        NS::Obj::OverlayRenderer::OnStart();

        m_movement = Owner()->FindComponent<NS::Game::Player::PlayerComponent>();
        // 無ければ null のまま。ボタンを積んでいない配置物でも裁定は続ける
        m_collisionInput = Owner()->FindComponent<CollisionInput>();
    }

    Breakable* ImpactResolver::FindOverlapped() const
    {
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return nullptr;
        }

        const NS::Core::Vector3 position = Owner()->Root().Position();
        const NS::Core::Vector3 velocity = m_movement->BodySlamVelocity();
        const float dt = NS::Platform::FrameTimer::FixedDelta();

        // この固定ステップで進んだ先で見る。今の位置だけでは手前で止められて重ならず、反発が起きない
        const NS::Phys::Capsule capsule{
            NS::Core::Vector3{position.x + velocity.x * dt, position.y + velocity.y * dt, position.z + velocity.z * dt},
            NS::Core::Vector3::UnitY,
            m_movement->CapsuleHalfHeight(),
            m_movement->CapsuleRadius()};
        const std::vector<JPH::BodyID> touching = scene->Physics().OverlapCapsule(capsule);

        // TODO: 壊せる物を総当たりで見ている。数十個までを想定。増えたら格子で絞る
        Breakable* nearest = nullptr;
        float nearestDistanceSq = 0.0f;
        scene->Objects().ForEachComponent<Breakable>([&](Breakable& breakable) {
            NS::Core::AABB bounds{};
            if (!TryGetTargetBounds(breakable, bounds))
            {
                return;
            }

            if (!IsTouching(touching, CurrentBodyOf(*breakable.Owner())))
            {
                return;
            }

            const float dx = bounds.Center.x - position.x;
            const float dy = bounds.Center.y - position.y;
            const float dz = bounds.Center.z - position.z;
            const float distanceSq = dx * dx + dy * dy + dz * dz;
            if (nearest == nullptr || distanceSq < nearestDistanceSq)
            {
                nearest = &breakable;
                nearestDistanceSq = distanceSq;
            }
        });
        return nearest;
    }

    bool ImpactResolver::FindHomingTarget(const NS::Core::Vector3& forward,
                                          float coneDegrees,
                                          float maxDistance,
                                          NS::Core::Vector3& outCenter,
                                          NS::Obj::ObjectRef preferred) const
    {
        NS::Core::Vector3 forwardDir{};
        if (Owner() == nullptr || !NS::Core::TryNormalizeHorizontal(forward, forwardDir))
        {
            return false;
        }
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return false;
        }

        // 角度は内積と余弦で比べる。非有限の角度は比較が偽になり、誰も拾わない
        const float minCosine = std::cos(NS::Core::ToRadians(NS::Core::Degrees{coneDegrees}).value);
        const NS::Core::Vector3 position = Owner()->Root().Position();

        // TODO: 壊せる物を総当たりで見ている。数十個までを想定。増えたら格子で絞る
        bool found = false;
        float nearestDistance = 0.0f;
        NS::Core::Vector3 nearestCenter{};
        bool preferredFound = false;
        NS::Core::Vector3 preferredCenter{};
        scene->Objects().ForEachComponent<Breakable>([&](Breakable& breakable) {
            NS::Core::AABB bounds{};
            if (!TryGetTargetBounds(breakable, bounds))
            {
                return;
            }

            const float dx = bounds.Center.x - position.x;
            const float dz = bounds.Center.z - position.z;
            const float distance = std::sqrt(dx * dx + dz * dz);
            // 真上と真下の相手は向きが決まらない
            if (!(distance >= NS::Core::k_Epsilon) || !(distance <= maxDistance))
            {
                return;
            }
            const float cosine = (dx * forwardDir.x + dz * forwardDir.z) / distance;
            if (!(cosine >= minCosine))
            {
                return;
            }
            if (preferred.IsSet() && breakable.Owner()->Id() == preferred.id)
            {
                preferredFound = true;
                preferredCenter = bounds.Center;
            }
            if (!found || distance < nearestDistance)
            {
                found = true;
                nearestDistance = distance;
                nearestCenter = bounds.Center;
            }
        });

        if (preferredFound)
        {
            outCenter = preferredCenter;
            return true;
        }
        if (found)
        {
            outCenter = nearestCenter;
        }
        return found;
    }

    bool ImpactResolver::FindSlamLineTarget(const NS::Core::Vector3& direction,
                                            float maxDistance,
                                            SlamLineTarget& outTarget) const
    {
        NS::Core::Vector3 lineDir{};
        if (Owner() == nullptr || m_movement == nullptr || !NS::Core::TryNormalizeHorizontal(direction, lineDir))
        {
            return false;
        }
        // 非数と無限の向きは正規化を通り抜ける
        if (!std::isfinite(lineDir.x) || !std::isfinite(lineDir.z))
        {
            return false;
        }
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return false;
        }

        // 非数・負・無限の距離では玉を掃けない
        if (!(maxDistance >= 0.0f) || !std::isfinite(maxDistance))
        {
            return false;
        }

        const NS::Core::Vector3 position = Owner()->Root().Position();
        const float playerRadius = m_movement->CapsuleRadius();
        // 突進は丸まった玉で進む。丸まっていれば玉の中心は根そのもの。立ち姿から丸まる時は下端を揃えて根を半長ぶん
        // 下げるので、立ち姿の下の球の中心が丸まった後の玉の中心になる
        const NS::Core::Vector3 ballCenter{position.x, position.y - m_movement->CapsuleHalfHeight(), position.z};
        // 届くかは裁定と同じく、自機の当たりの玉と相手の body の実物の形で見る。外接箱を水平に見ると、中心の高い
        // 大きな球の端では、玉が触れずに横を通るのに届くと出る
        const NS::Phys::PhysicsScene& physics = scene->Physics();
        const std::vector<JPH::BodyID> swept =
            physics.OverlapCapsule(SweptBall(ballCenter, lineDir, maxDistance, playerRadius));

        // TODO: 壊せる物を総当たりで見ている。数十個までを想定。増えたら格子で絞る
        bool found = false;
        SlamLineTarget first{};
        scene->Objects().ForEachComponent<Breakable>([&](Breakable& breakable) {
            NS::Core::AABB bounds{};
            if (!TryGetTargetBounds(breakable, bounds))
            {
                return;
            }

            const LineOffset line = MeasureLineOffset(position, bounds, lineDir, playerRadius);
            // 真横と後ろの相手は線の先に居ない
            if (!(line.along > 0.0f))
            {
                return;
            }
            // 比が 1 を超える相手は、線を進む自機の縁が相手の外接箱の縁に届かない。丸めた値で見ると 1 に張り付いて拾う
            if (!(line.ratio <= 1.0f))
            {
                return;
            }
            const JPH::BodyID body = CurrentBodyOf(*breakable.Owner());
            if (!IsTouching(swept, body))
            {
                return;
            }
            // 最初に触れる相手は、中心の近さでなく玉が触れるまでに進む距離で決める。突進はそこで止まって当たる
            const float contact = FirstTouchDistance(physics, ballCenter, lineDir, maxDistance, playerRadius, body);
            if (!found || contact < first.contact)
            {
                found = true;
                first = SlamLineTarget{.target = NS::Obj::ObjectRef{breakable.Owner()->Id()},
                                       .bounds = bounds,
                                       .origin = position,
                                       .direction = lineDir,
                                       .along = line.along,
                                       .offset = line.ratio,
                                       .contact = contact};
            }
        });

        if (found)
        {
            outTarget = first;
        }
        return found;
    }

    void ImpactResolver::OnUpdate()
    {
        m_didRebound = false;
        m_didBreak = false;
        // フラッシュの減衰は早期 return より前に置く。凍結中のフレームもここまでは来るので、止まっている間も白が薄れる
        if (m_centerHitFlashRemaining > 0)
        {
            --m_centerHitFlashRemaining;
        }
        // 振動も早期 return より前で毎フレーム書く。書かれなかったフレームは Gamepad::Update が 0 にする
        if (m_padRunning)
        {
            ++m_padElapsed;
            WritePadVibration();
        }
        if (m_movement == nullptr)
        {
            return;
        }

        // 止まっている間は新しい衝突を見ない。凍った自機は重なったままなので、見ると毎フレーム検知し直す
        if (m_hitStopRemaining > 0)
        {
            --m_hitStopRemaining;
            if (m_hitStopRemaining == 0)
            {
                ReleaseHitStop();
                return;
            }
            ApplyFreezeVibration();
            return;
        }

        if (m_freezePendingSteps > 0)
        {
            BeginFreeze(m_freezePendingSteps);
            m_freezePendingSteps = 0;
            return;
        }

        if (m_recoverRemaining > 0)
        {
            RecoverScale();
        }

        // 押していない接触は物理の停止だけで済ませるため、体当たり中でないフレームは裁定しない
        if (!m_movement->IsBodySlamming())
        {
            return;
        }

        Breakable* hit = FindOverlapped();
        if (hit == nullptr)
        {
            return;
        }

        NS::Core::AABB bounds{};
        if (!TryGetColliderBounds(*hit->Owner(), bounds))
        {
            return;
        }

        const NS::Core::Vector3 position = Owner()->Root().Position();
        // 箱へ押し付けられたフレームは実速度が 0 に潰されるため、突進の狙いの速度で向きと貫通後の速度を決める
        const NS::Core::Vector3 velocity = m_movement->BodySlamVelocity();

        // 弾かれる向きは箱と自機の並びで決まる。水平だけを見て、上向きは反動の高さから出す
        float awayX = position.x - bounds.Center.x;
        float awayZ = position.z - bounds.Center.z;
        float lengthSq = awayX * awayX + awayZ * awayZ;
        // 箱の中心へ重なると向きが決まらない。進んできた向きの逆へ弾く
        if (lengthSq < NS::Core::k_Epsilon * NS::Core::k_Epsilon)
        {
            awayX = -velocity.x;
            awayZ = -velocity.z;
            lengthSq = awayX * awayX + awayZ * awayZ;
            if (lengthSq < NS::Core::k_Epsilon * NS::Core::k_Epsilon)
            {
                return;
            }
        }
        const float invLength = 1.0f / std::sqrt(lengthSq);
        awayX *= invLength;
        awayZ *= invLength;

        // 箱へ向かっているフレームだけ弾く。離れていく間も弾くと、重なりが解けるまで毎フレーム掛かり直す
        if (velocity.x * awayX + velocity.z * awayZ >= 0.0f)
        {
            return;
        }

        const float charge01 = m_movement->BodySlamCharge01();

        const float mass = MassOf(*hit->Owner());
        const float massFactor = mass / (mass + 1.0f);

        // ボタン未搭載は係数 1.0 の素通し。段は中心近くと記録するが、白の光と止めの倍率は掛けない
        const float offset01 = HitOffset01(position, bounds, velocity, m_movement->CapsuleRadius());
        float chargeFactor = 1.0f;
        float positionFactor = 1.0f;
        HitTier tier = HitTier::Center;
        bool centerHit = false;
        if (m_collisionInput != nullptr)
        {
            chargeFactor = m_collisionInput->ChargeFactorFor(charge01);
            positionFactor = m_collisionInput->PositionFactorFor(offset01);
            tier = m_collisionInput->HitTierFor(offset01);
            centerHit = tier == HitTier::Center;
        }
        // 最終威力 = チャージ倍率 × 当たり位置係数。破壊の判定だけでなく反発・発射・揺れも威力で作る
        const float power = chargeFactor * positionFactor;
        m_lastCharge01 = charge01;
        m_lastPositionFactor = positionFactor;
        m_lastPower = power;
        m_wasCenterHit = centerHit;
        float hitStopScale = 1.0f;
        if (centerHit)
        {
            hitStopScale = m_centerHitStopScale;
        }
        NS_LOG_INFO(Game,
                    "威力の内訳: 溜め {} × 当たり位置 {} = {} 溜め量 {} 中心からの横ずれ {}",
                    chargeFactor,
                    positionFactor,
                    power,
                    charge01,
                    offset01);

        // 明けたフレームの反発と貫通速度を通常移動に乗せるため、凍結より先に突進を打ち切る
        m_movement->CancelBodySlam();

        m_pendingTarget = NS::Obj::ObjectRef{hit->Owner()->Id()};
        m_pendingTargetHome = hit->Owner()->Root().Position();
        // 飛んでいる相手の根は物理が毎フレーム書くので、食い込み・振動・元位置へ戻す書き込みは効かない。置かれた相手に絞る
        const LaunchedBody* launched = hit->Owner()->FindComponent<LaunchedBody>();
        m_pendingTargetPlaced = launched == nullptr || launched->Phase() == LaunchPhase::Resting;
        // 相手は突進の向きへ飛ばす。中心の並びで飛ばすと、横ずれのある当たりが狙いと別の所へ飛ぶ
        // 突進の水平の速さがほぼ 0 で向きが決まらない時だけ、中心の並びの向きへ飛ばす
        NS::Core::Vector3 launchDir{-awayX, 0.0f, -awayZ};
        NS::Core::Vector3 slamDir{};
        if (NS::Core::TryNormalizeHorizontal(velocity, slamDir))
        {
            launchDir = slamDir;
        }
        m_pendingImpactDir = launchDir;
        // 反発の質量因子の残り。動きは軽い側が受け取るので、重い物ほど揺れない
        m_pendingShakeAmplitude = m_shakeAmplitude / (1.0f + mass);

        // 破壊を許可していない間は耐久を見ない。壊れる相手も押し飛ばしと反発へ回る
        int stopSteps = 0;
        if (m_breakEnabled && hit->Toughness() <= power)
        {
            m_pendingBreak = true;
            // 貫通は相手を飛ばさず、自機も反動しない
            // 前の当たりの曲線を残すと、この当たりの記録に使っていない曲線が載る
            m_pendingLaunchArc = LaunchArc{};
            m_pendingReboundArc = NS::Game::Player::ReboundArc{};
            // 向きを保ったまま減速する。倍率は相手の質量に依らない
            m_pendingSelfVelocity = velocity * m_breakSpeedScale;
            m_didBreak = true;
            NS_LOG_INFO(Game, "貫通: 耐久 {} 威力 {} 中心近く {}", hit->Toughness(), power, centerHit);
            stopSteps = SecondsToSteps(m_breakStopSeconds * hitStopScale);
        }
        else
        {
            m_pendingBreak = false;
            // 質量因子 mass/(mass+1) は質量が大きいほど 1 へ寄る。軽い物は勢いを持っていくのでほとんど返らない
            // 2 倍して質量 1 で 1 にし、欄を質量 1・威力 1 の高さと距離で持つ
            // 高さと距離に同じ倍率を掛け、威力と質量が変わっても弾かれ始めの角度を揃える
            // TODO: 質量 0.5 より軽い物では自機の返りが 0 に近づく。軽い物を置く時は、先に高さと距離の下限を足す
            const float reboundScale = power * 2.0f * massFactor;
            // 中心近くの当たりだけ距離を伸ばし、高さは変えない。真ん中に当てた時は後ろへ飛ぶ
            float reboundDistance = m_reboundDistance * reboundScale;
            if (centerHit)
            {
                reboundDistance *= m_centerHitReboundDistanceScale;
            }
            m_pendingReboundArc = NS::Game::Player::ReboundArc{.direction = NS::Core::Vector3{awayX, 0.0f, awayZ},
                                                               .apexHeight = m_reboundApexHeight * reboundScale,
                                                               .distance = reboundDistance};
            m_pendingSelfVelocity = m_movement->ReboundVelocityFor(m_pendingReboundArc);

            // 指数の範囲は 0〜1。負にすると重い物ほど飛ぶ逆転になる
            float massExponent = m_launchMassExponent;
            if (!std::isfinite(massExponent))
            {
                massExponent = 1.0f;
            }
            massExponent = NS::Core::Clamp(massExponent, 0.0f, 1.0f);

            // 威力は距離に線形に効き、質量で割ると重い物ほど飛ばない。高さは距離と同じ比で伸ばし、打ち上げの角度を揃える
            const float launchScale = power / std::pow(mass, massExponent);
            m_pendingLaunchArc = LaunchArc{.direction = launchDir,
                                           .distance = m_launchDistance * launchScale,
                                           .apexHeight = m_launchApexHeight * launchScale,
                                           .fallGravityScale = m_launchFallGravityScale,
                                           .apexBandSpeed = m_launchApexBandSpeed,
                                           .apexBandGravityScale = m_launchApexBandGravityScale};

            m_didRebound = true;
            NS_LOG_INFO(Game,
                        "衝突: 質量 {} 耐久 {} 反動の高さ {} 距離 {} 押し飛ばしの距離 {} 高さ {} 中心近く {}",
                        mass,
                        hit->Toughness(),
                        m_pendingReboundArc.apexHeight,
                        m_pendingReboundArc.distance,
                        m_pendingLaunchArc.distance,
                        m_pendingLaunchArc.apexHeight,
                        centerHit);
            stopSteps = ComputeHitStopSteps(power, mass, hitStopScale);
        }

        PrepareHitReturns(tier, m_collisionInput != nullptr, power, massFactor, offset01, stopSteps);

        // 止めるフレーム数が決まってから控える。止めが 0 フレームの当たりも残すので、下の return より手前に置く
        m_lastImpact.sequence += 1;
        m_lastImpact.targetId = m_pendingTarget.id;
        m_lastImpact.power = power;
        m_lastImpact.charge01 = charge01;
        m_lastImpact.positionFactor = positionFactor;
        m_lastImpact.offset01 = offset01;
        m_lastImpact.tier = tier;
        m_lastImpact.hitStopSteps = stopSteps;
        m_lastImpact.centerHit = centerHit;
        m_lastImpact.broke = m_pendingBreak;
        m_lastImpact.selfVelocity = m_pendingSelfVelocity;
        m_lastImpact.reboundApexHeight = m_pendingReboundArc.apexHeight;
        m_lastImpact.launchVelocity = LaunchArcInitialVelocity(m_pendingLaunchArc);
        m_lastImpact.launchDistance = m_pendingLaunchArc.distance;
        m_lastImpact.launchApexHeight = m_pendingLaunchArc.apexHeight;
        m_lastImpact.impactDir = m_pendingImpactDir;
        m_lastImpact.targetPos = m_pendingTargetHome;

        if (stopSteps <= 0)
        {
            ReleaseHitStop();
            return;
        }

        // 凍結は次のフレームから。今回は移動を最後まで走らせ、自機が箱へ触れてから止まる
        m_freezePendingSteps = stopSteps;
    }

    void ImpactResolver::BeginFreeze(int stopSteps)
    {
        // 自機を寝かせて凍らせる。ObjectList::UpdateObjects は active をその場で見るので同じフレームから効く
        m_hitStopRemaining = stopSteps;
        m_hitStopTotal = stopSteps;
        m_movement->SetActive(false);
        NS_LOG_INFO(Game, "ヒットストップ: {} フレーム", stopSteps);

        // 潰れは反発の前半。進行方向の厚みを潰し、代わりに高さを伸ばす
        // 戻りの最中に次の衝突が来たら、控え済みの元の形をそのまま使い続ける
        if (m_recoverRemaining == 0)
        {
            m_scaleHome = RootTransform().Scale();
        }
        m_recoverRemaining = 0;
        m_scaleHeld = true;
        // 貫通は押し勝っている側なので潰さない。潰れは押し返されている反発だけの絵
        if (!m_pendingBreak)
        {
            RootTransform().SetScale(ScaledAlongImpact(m_squashThickness, m_squashHeight));
        }

        StartHitReturns();

        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return;
        }

        // 力が伝わった瞬間の絵。凍結の頭で置かれた相手を発射方向へ食い込ませて止める
        NS::Obj::GameObject* target = scene->Objects().FindObject(m_pendingTarget);
        if (m_pendingTargetPlaced && target != nullptr)
        {
            target->Root().SetPosition(m_pendingTargetHome + m_pendingImpactDir * m_pushInDistance);
            // 相手も自機と同じく、押し返されている反発の時だけ縮める
            if (!m_pendingBreak)
            {
                ShrinkPlacedTarget(*target);
            }
        }
    }

    void ImpactResolver::PrepareHitReturns(
        HitTier tier, bool tiered, float power, float massFactor, float offset01, int stopSteps)
    {
        // 段の無い台は白と寄りと傾きを出さず、揺れの倍率も掛けない
        const bool center = tiered && tier == HitTier::Center;
        const bool nearMiss = tiered && tier == HitTier::Near;
        const bool wide = tiered && tier == HitTier::Wide;

        // 最初の振れの大きさは全段で同じ式。反発と同じ質量因子を掛け、中心近くだけ段の倍率を掛ける
        float swing = m_cameraShakeScale * power * massFactor;
        m_pendingFlashSteps = 0;
        if (center)
        {
            swing *= m_centerHitShakeScale;
            m_pendingFlashSteps = m_centerHitFlashSteps;
        }

        // 中心近くと惜しいは縦だけを毎フレーム入れ替え、止めのフレーム数で収める
        m_pendingShake = NS::Obj::CameraShakeDesc{
            .sideAmplitude = 0.0f,
            .upAmplitude = swing,
            .frames = stopSteps,
            .longestFlipFrames = 1,
            .firstSideDirection = m_pendingReboundArc.direction,
            .seed = ShakeSeed(m_pendingTarget.id, offset01, m_pendingImpactDir, m_pendingTargetHome),
        };
        if (wide)
        {
            // 横と縦を合わせた長さが最初の振れの大きさになるよう、比で分ける
            const float side = swing / std::sqrt(1.0f + m_wideShakeUpOverSide * m_wideShakeUpOverSide);
            m_pendingShake.sideAmplitude = side;
            m_pendingShake.upAmplitude = side * m_wideShakeUpOverSide;
            m_pendingShake.frames = m_wideShakeFrames;
            m_pendingShake.longestFlipFrames = m_wideShakeLongestFlipFrames;
        }

        // 寄りの無い段も倍率 1 の設定を渡し、前の当たりの寄りを残さない
        m_pendingZoomRoll = NS::Obj::CameraZoomRollDesc{.rollDirection = m_pendingImpactDir};
        if (center)
        {
            m_pendingZoomRoll.zoom = m_centerHitZoom;
            m_pendingZoomRoll.rollDegrees = m_centerHitRollDegrees;
            m_pendingZoomRoll.holdFrames = stopSteps;
            m_pendingZoomRoll.returnFrames = m_zoomRollReturnFrames;
        }
        if (nearMiss)
        {
            // 寄りは 1 を超えた分に割合を掛ける。倍率そのものに掛けると 1 未満の引きになる
            m_pendingZoomRoll.zoom = 1.0f + (m_centerHitZoom - 1.0f) * m_nearHitReturnRatio;
            m_pendingZoomRoll.rollDegrees = m_centerHitRollDegrees * m_nearHitReturnRatio;
            m_pendingZoomRoll.holdFrames = PullBackFrames(stopSteps, m_nearHitPullBackRatio);
            m_pendingZoomRoll.returnFrames = m_zoomRollReturnFrames;
        }

        // 当たりの記録は検知のフレームに読まれるので、傾きの向きもここで今のカメラから決める
        float rollSign = 1.0f;
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene != nullptr && scene->CameraBrain() != nullptr)
        {
            rollSign = scene->CameraBrain()->SideSignOf(m_pendingZoomRoll.rollDirection);
        }
        // 振動は段ごとにモーターを分ける。強さは質量と威力で変えない。中心近くと惜しいの長さは止めで結ぶ
        m_pendingPad = PadVibrationPlan{};
        if (center)
        {
            m_pendingPad.start.left = m_centerHitPadStrength;
            m_pendingPad.fadeFrames = stopSteps;
            m_pendingPad.frames = stopSteps;
        }
        if (nearMiss)
        {
            // 減る傾きは中心近くと同じにし、寄りと同じフレームで切る
            m_pendingPad.start.left = m_centerHitPadStrength * m_nearHitReturnRatio;
            m_pendingPad.fadeFrames = stopSteps;
            m_pendingPad.frames = PullBackFrames(stopSteps, m_nearHitPullBackRatio);
        }
        if (wide)
        {
            m_pendingPad.start.right = m_widePadStrength;
            m_pendingPad.fadeFrames = m_wideShakeFrames;
            m_pendingPad.frames = m_wideShakeFrames;
        }

        m_lastImpact.cameraShake = NS::Core::Vector2{m_pendingShake.sideAmplitude, m_pendingShake.upAmplitude}.Length();
        m_lastImpact.flashStart = m_pendingFlashSteps;
        m_lastImpact.zoomStart = m_pendingZoomRoll.zoom;
        m_lastImpact.rollStart = m_pendingZoomRoll.rollDegrees * rollSign;
        m_lastImpact.padStart = m_pendingPad.start;
    }

    void ImpactResolver::StartHitReturns()
    {
        m_centerHitFlashRemaining = m_pendingFlashSteps;
        m_pad = m_pendingPad;
        m_padElapsed = 0;
        m_padRunning = true;
        WritePadVibration();

        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr || scene->CameraBrain() == nullptr)
        {
            return;
        }
        NS::Obj::CameraBrain& brain = *scene->CameraBrain();
        if (m_pendingShake.frames > k_MaxShakeFrames)
        {
            NS_LOG_WARN(Game,
                        "揺れのフレーム数 {} が上限 {} を超えていて、揺らさなかった",
                        m_pendingShake.frames,
                        k_MaxShakeFrames);
        }
        else if (!brain.StartShake(m_pendingShake))
        {
            NS_LOG_WARN(Game,
                        "揺れの設定が壊れていて、揺らさなかった: 横 {} 縦 {} フレーム数 {} 最長 {}",
                        m_pendingShake.sideAmplitude,
                        m_pendingShake.upAmplitude,
                        m_pendingShake.frames,
                        m_pendingShake.longestFlipFrames);
        }
        if (!brain.StartZoomRoll(m_pendingZoomRoll))
        {
            NS_LOG_WARN(Game,
                        "寄りと傾きの設定が壊れていて、寄せなかった: 倍率 {} 傾き {} 保つ {} 戻す {}",
                        m_pendingZoomRoll.zoom,
                        m_pendingZoomRoll.rollDegrees,
                        m_pendingZoomRoll.holdFrames,
                        m_pendingZoomRoll.returnFrames);
        }
    }

    void ImpactResolver::WritePadVibration()
    {
        NS::Platform::GamepadVibration speed{};
        if (m_padElapsed < m_pad.frames)
        {
            const float fade =
                static_cast<float>(m_pad.fadeFrames - m_padElapsed) / static_cast<float>(m_pad.fadeFrames);
            speed.left = m_pad.start.left * fade;
            speed.right = m_pad.start.right * fade;
        }
        else
        {
            // 終わりのフレームも 0 を書く。書かないと次の Input::Update までは前の値が読める
            m_padRunning = false;
        }
        // 以後のフレームは始めの値から 0 へ減るだけなので、範囲の外になるのは始めの値が外の時だけ
        if (!NS::Platform::Input::Get().Gamepad(0).SetVibration(speed.left, speed.right))
        {
            NS_LOG_WARN(Game, "パッドの振動の速さが 0〜1 の外で、震わせなかった: 左 {} 右 {}", speed.left, speed.right);
            m_padRunning = false;
        }
    }

    void ImpactResolver::OnEndPlay()
    {
        // 基底が重ね描きの登録簿から自分を外す
        NS::Obj::OverlayRenderer::OnEndPlay();

        // 凍結の途中で裁定が外れても、移動が止まったまま残らないようにする
        if (m_movement != nullptr)
        {
            m_movement->SetActive(true);
        }
        RestoreTargetShape();
        m_centerHitFlashRemaining = 0;
        // 書くフレーム数 0 の振動を書くと 0 が入り、プレイを終えたフレームの値が残らない
        m_pad = PadVibrationPlan{};
        WritePadVibration();
        // 揺れと寄りは CameraBrain が持つ。止めないとプレイを終えた後の視点にずれが残る
        if (Owner() != nullptr && Owner()->OwningScene() != nullptr)
        {
            if (NS::Obj::CameraBrain* brain = Owner()->OwningScene()->CameraBrain())
            {
                brain->StopShakeAndZoomRoll();
            }
        }
    }

    void ImpactResolver::OnRenderOverlay(const NS::Gfx::RenderContext& ctx)
    {
        if (m_centerHitFlashRemaining <= 0)
        {
            return;
        }
        // ScreenFade は黒の固定色と暗転の状態機械で、白の瞬間減衰には流用できないためここで直接描く
        const float decay = static_cast<float>(m_centerHitFlashRemaining) / static_cast<float>(m_centerHitFlashSteps);
        ctx.renderer->DrawFullscreenColor(NS::Core::Color{1.0f, 1.0f, 1.0f, m_centerHitFlashAlpha * decay});
    }

    int ImpactResolver::SecondsToSteps(float seconds) const noexcept
    {
        // 整数のフレームへ丸めるので、同じ秒の指定は毎回同じ長さ止まる
        const float raw = seconds / NS::Platform::FrameTimer::FixedDelta();
        if (!std::isfinite(raw))
        {
            return 0;
        }
        return NS::Core::Clamp(static_cast<int>(std::lround(raw)), 0, MaxHitStopSteps());
    }

    int ImpactResolver::MaxHitStopSteps() const noexcept
    {
        const float raw = m_hitStopMaxSeconds / NS::Platform::FrameTimer::FixedDelta();
        if (!std::isfinite(raw) || raw <= 0.0f)
        {
            return 0;
        }
        return static_cast<int>(std::lround(raw));
    }

    void ImpactResolver::ReleaseHitStop()
    {
        m_movement->SetActive(true);
        const bool wasBreak = m_pendingBreak;
        m_pendingBreak = false;
        if (wasBreak)
        {
            m_movement->SetVelocity(m_pendingSelfVelocity);
        }
        else if (!m_movement->BeginRebound(m_pendingReboundArc))
        {
            // 欄が曲線にならない値の時だけ通る。書かないと、止める前の最後のフレームの速度のまま動き出す
            m_movement->SetVelocity(m_pendingSelfVelocity);
            NS_LOG_WARN(Game,
                        "反動が曲線にならず、自機を弾けなかった: 高さ {} 距離 {}",
                        m_pendingReboundArc.apexHeight,
                        m_pendingReboundArc.distance);
        }
        if (m_scaleHeld)
        {
            // 解放の伸びが衝突の後半。反発は自機が上へ大きく弾かれるので縦へ、貫通は突き抜ける進行の軸へ伸ばす
            if (wasBreak)
            {
                m_stretchScale = ScaledAlongImpact(m_stretchAlong, 1.0f);
            }
            else
            {
                m_stretchScale = NS::Core::Vector3{m_scaleHome.x, m_scaleHome.y * m_stretchAlong, m_scaleHome.z};
            }
            RootTransform().SetScale(m_stretchScale);
            m_recoverRemaining = m_stretchRecoverSteps;
            m_scaleHeld = false;
        }
        // 相手は元の形で飛ぶ
        RestoreTargetShape();

        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return;
        }

        // 相手は id で引き直す。止まっている数フレームの間に消されていたら残りだけ諦める
        NS::Obj::GameObject* target = scene->Objects().FindObject(m_pendingTarget);
        if (target == nullptr)
        {
            return;
        }

        // 食い込みと振動は見せるための動き。曲線の起点がずれないよう、置かれていた相手は元位置へ厳密に戻してから飛ばす
        // 飛んでいる相手は戻さず、今の位置から曲線を引き直す
        if (m_pendingTargetPlaced)
        {
            target->Root().SetPosition(m_pendingTargetHome);
        }

        // 跡は破壊と押し飛ばしの両方で出す。片方だけ何も残らないと結果が非対称になる
        bool floorFound = false;
        NS::Core::Vector3 markPosition{0.0f, 0.0f, 0.0f};
        {
            // 起点は相手の底の下。中心から始めると相手自身の当たりに 0 距離で当たる
            // 水平は明けのフレームの根の位置。飛んでいる相手は止めの間も進んでいて、検知のフレームの位置には居ない
            NS::Core::Vector3 probe = target->Root().Position();
            NS::Core::AABB targetBounds{};
            if (TryGetColliderBounds(*target, targetBounds))
            {
                // 底から 1cm 下げる。誤差で相手自身に当たらない最小の隙間
                probe.y = targetBounds.Center.y - targetBounds.Extents.y - 0.01f;
            }

            float dist = 0.0f;
            if (scene->Physics().Raycast(probe, NS::Core::Vector3{0.0f, -1.0f, 0.0f}, m_markProbeDistance, dist))
            {
                floorFound = true;
                // 床の上面から 2cm 浮かせる。面がぴったり重なるとちらつく
                markPosition = NS::Core::Vector3{probe.x, probe.y - dist + 0.02f, probe.z};
            }
        }

        // 積み忘れた配置物でも押し飛ばしと破壊が効くよう、無ければその場で足す
        LaunchedBody* body = target->FindComponent<LaunchedBody>();
        if (body == nullptr)
        {
            body = target->AddComponent<LaunchedBody>();
        }

        if (wasBreak)
        {
            body->Shatter();
            if (floorFound)
            {
                (void)ImpactMark::SpawnAt(scene, markPosition);
            }
            return;
        }

        body->Launch(m_pendingLaunchArc);
        if (floorFound)
        {
            (void)ImpactMark::SpawnAt(scene, markPosition);
        }
    }

    void ImpactResolver::ApplyFreezeVibration()
    {
        // 飛んでいる相手は止めずに飛び続ける。根は物理が書くので、揺らしても絵に出ない
        if (!m_pendingTargetPlaced)
        {
            return;
        }
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        NS::Obj::GameObject* target = scene->Objects().FindObject(m_pendingTarget);
        if (target == nullptr || m_hitStopTotal <= 0)
        {
            return;
        }

        // フレーム数の偶奇で往復し、残りフレーム数で減衰する。乱数を使わないので同じ入力は同じ絵になる
        const float sign = 1.0f - 2.0f * static_cast<float>(m_hitStopRemaining % 2);
        const float decay = static_cast<float>(m_hitStopRemaining) / static_cast<float>(m_hitStopTotal);
        const float along = m_pushInDistance + m_pendingShakeAmplitude * sign * decay;
        target->Root().SetPosition(m_pendingTargetHome + m_pendingImpactDir * along);
    }

    void ImpactResolver::RecoverScale()
    {
        --m_recoverRemaining;
        if (m_recoverRemaining <= 0)
        {
            // 補間の残差を残さない。控えた元の値をそのまま書いて形を確定させる
            RootTransform().SetScale(m_scaleHome);
            return;
        }
        // 前半は伸びた形から、元の形を伸びと反対の側へ 伸びの量 × 行き過ぎの割合 だけ越えた所まで進む
        // 後半はそこから元の形へ戻る
        const float total = static_cast<float>(m_stretchRecoverSteps);
        const float half = total * 0.5f;
        const float elapsed = total - static_cast<float>(m_recoverRemaining);
        const NS::Core::Vector3 overshoot = m_scaleHome - (m_stretchScale - m_scaleHome) * m_stretchOvershoot;
        if (elapsed <= half)
        {
            RootTransform().SetScale(m_stretchScale + (overshoot - m_stretchScale) * (elapsed / half));
            return;
        }
        RootTransform().SetScale(overshoot + (m_scaleHome - overshoot) * ((elapsed - half) / (total - half)));
    }

    NS::Core::Vector3 ImpactResolver::ScaledAlongImpact(float along, float height) const noexcept
    {
        const NS::Core::Vector3 factors = AlongImpactFactors(along, height);
        return NS::Core::Vector3{m_scaleHome.x * factors.x, m_scaleHome.y * factors.y, m_scaleHome.z * factors.z};
    }

    NS::Core::Vector3 ImpactResolver::AlongImpactFactors(float along, float height) const noexcept
    {
        // 衝突は水平でしか起きない。進行の軸成分の 2 乗で倍率を混ぜ、軸に載った衝突では素の倍率になる
        const float dx2 = m_pendingImpactDir.x * m_pendingImpactDir.x;
        const float dz2 = m_pendingImpactDir.z * m_pendingImpactDir.z;
        return NS::Core::Vector3{1.0f + (along - 1.0f) * dx2, height, 1.0f + (along - 1.0f) * dz2};
    }

    void ImpactResolver::ShrinkPlacedTarget(NS::Obj::GameObject& target)
    {
        NS::Obj::MeshRenderer* look = target.FindComponent<NS::Obj::MeshRenderer>();
        if (look == nullptr)
        {
            return;
        }
        // 当たりは根の世界のスケールから形を作るので、根は潰さず描く形だけを縮める
        // 前と今を揃えて書く。MeshRenderer の OnUpdate の前後に依らず、元の形から補間せずに縮んだ形で描く
        if (!look->SnapDrawScale(AlongImpactFactors(m_squashThickness, m_squashHeight)))
        {
            NS_LOG_WARN(Game,
                        "潰れの厚みか伸び上がりが有限の正でなく、相手を縮めなかった: 厚み {} 伸び上がり {}",
                        m_squashThickness,
                        m_squashHeight);
            return;
        }
        m_targetShapeHeld = true;
    }

    void ImpactResolver::RestoreTargetShape()
    {
        if (!m_targetShapeHeld)
        {
            return;
        }
        m_targetShapeHeld = false;
        if (Owner() == nullptr)
        {
            return;
        }
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        NS::Obj::GameObject* target = scene->Objects().FindObject(m_pendingTarget);
        if (target == nullptr)
        {
            return;
        }
        NS::Obj::MeshRenderer* look = target->FindComponent<NS::Obj::MeshRenderer>();
        if (look == nullptr)
        {
            return;
        }
        // 縮めた時と同じく前と今を揃えて書き、縮んだ形から補間しない
        (void)look->SnapDrawScale(NS::Core::Vector3{1.0f, 1.0f, 1.0f});
    }

    int ImpactResolver::ComputeHitStopSteps(float power, float mass, float hitStopScale) const noexcept
    {
        // 質量差をそのままフレーム数に出すと停止が伸びすぎるので平方根で圧縮する
        const float raw =
            m_hitStopBaseSeconds * power * std::sqrt(mass) / NS::Platform::FrameTimer::FixedDelta() * hitStopScale;
        if (!std::isfinite(raw))
        {
            return 0;
        }
        const int steps = static_cast<int>(std::lround(raw));
        return NS::Core::Clamp(steps, 0, MaxHitStopSteps());
    }

    NS_CLASS(ImpactResolver)
} // namespace NS::Game::Level
