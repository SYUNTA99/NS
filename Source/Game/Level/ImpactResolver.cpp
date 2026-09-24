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
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Physics/PhysicsScene.h"
#include "Runtime/Platform/Clock.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace NS::Game::Level
{
    namespace
    {
        // 相手の中心からの横ずれ 0..1。OnUpdate へ式を埋めると当たり判定の流れが読めなくなる
        // 半径は AABB を突進方向に直交する軸へ投影した半幅。球と傾いた箱は外接箱で測るので実際の縁より広く出る
        // 水平が 0 の枝は要らない。向かっていないフレームは内積の判定で先に返しており、水平が 0 のフレームもそこへ入る
        [[nodiscard]] float HitOffset01(const NS::Core::Vector3& position,
                                        const NS::Core::AABB& bounds,
                                        const NS::Core::Vector3& velocity) noexcept
        {
            const float toX = bounds.Center.x - position.x;
            const float toZ = bounds.Center.z - position.z;
            const float invSpeed = 1.0f / std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
            const float dirX = velocity.x * invSpeed;
            const float dirZ = velocity.z * invSpeed;
            const float along = toX * dirX + toZ * dirZ;
            const float lateralX = toX - along * dirX;
            const float lateralZ = toZ - along * dirZ;
            const float lateral = std::sqrt(lateralX * lateralX + lateralZ * lateralZ);
            const float radius = std::abs(dirZ) * bounds.Extents.x + std::abs(dirX) * bounds.Extents.z;
            // 半幅 0 の相手では割れない。中心扱いへ倒す
            if (!(radius > 0.0f))
            {
                return 0.0f;
            }
            return NS::Core::Clamp(lateral / radius, 0.0f, 1.0f);
        }

        [[nodiscard]] JPH::BodyID CurrentBodyOf(const NS::Obj::GameObject& object) noexcept
        {
            // 飛んでいる間は collider の body が外れて無効になる。LaunchedBody が作った動的 body を先に見る
            if (const LaunchedBody* launched = object.FindComponent<LaunchedBody>();
                launched != nullptr && launched->IsFlying())
            {
                return launched->BodyId();
            }
            if (const NS::Obj::Collider* collider = object.FindComponent<NS::Obj::Collider>())
            {
                return collider->BodyId();
            }
            return JPH::BodyID{};
        }

        [[nodiscard]] bool IsTouching(const std::vector<JPH::BodyID>& touching, JPH::BodyID id)
        {
            return std::find(touching.begin(), touching.end(), id) != touching.end();
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
            if (!breakable.IsActive())
            {
                return;
            }

            // トリガの箱は通り抜ける体積なのでぶつかる相手にならない
            const NS::Obj::BoxCollider* box = breakable.Owner()->FindComponent<NS::Obj::BoxCollider>();
            if (box != nullptr && box->IsTrigger())
            {
                return;
            }

            if (!IsTouching(touching, CurrentBodyOf(*breakable.Owner())))
            {
                return;
            }

            NS::Core::AABB bounds{};
            if (!TryGetColliderBounds(*breakable.Owner(), bounds))
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

    void ImpactResolver::OnUpdate()
    {
        m_didRebound = false;
        m_didBreak = false;
        // フラッシュの減衰は早期 return より前に置く。凍結中のフレームもここまでは来るので、止まっている間も白が薄れる
        if (m_centerHitFlashRemaining > 0)
        {
            --m_centerHitFlashRemaining;
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

        // 弾かれる向きは箱と自機の並びで決まる。水平だけを見て、上向きは別の値で足す
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

        const float mass = hit->Mass();
        const float massFactor = mass / (mass + 1.0f);

        // ボタン未搭載 (null) は係数 1.0 の素通し
        const float offset01 = HitOffset01(position, bounds, velocity);
        float chargeFactor = 1.0f;
        float positionFactor = 1.0f;
        bool centerHit = false;
        if (m_collisionInput != nullptr)
        {
            chargeFactor = m_collisionInput->ChargeFactorFor(charge01);
            positionFactor = m_collisionInput->PositionFactorFor(offset01);
            centerHit = m_collisionInput->IsCenterHit(positionFactor);
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
            m_centerHitFlashRemaining = m_centerHitFlashSteps;
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
        m_pendingImpactDir = NS::Core::Vector3{-awayX, 0.0f, -awayZ};
        // 反発の質量因子の残り。動きは軽い側が受け取るので、重い物ほど揺れない
        m_pendingShakeAmplitude = m_shakeAmplitude / (1.0f + mass);
        m_pendingShakeStrength = m_cameraShakeScale * power * massFactor;

        // 破壊を許可していない間は耐久を見ない。壊れる相手も押し飛ばしと反発へ回る
        int stopSteps = 0;
        if (m_breakEnabled && hit->Toughness() <= power)
        {
            m_pendingBreak = true;
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
            float rebound = m_reboundSpeed * power * massFactor;
            rebound = std::max(rebound, 0.0f);

            // 指数の範囲は 0〜1。負にすると重い物ほど飛ぶ逆転になる
            float massExponent = m_launchMassExponent;
            if (!std::isfinite(massExponent))
            {
                massExponent = 1.0f;
            }
            massExponent = NS::Core::Clamp(massExponent, 0.0f, 1.0f);

            // 質量で割ると重い物ほど飛ばない
            float launch = m_launchSpeed * power / std::pow(mass, massExponent);
            launch = NS::Core::Clamp(launch, 0.0f, std::max(m_launchMaxSpeed, 0.0f));

            m_pendingSelfVelocity = NS::Core::Vector3{awayX * rebound, m_reboundUpSpeed, awayZ * rebound};
            m_pendingLaunchVelocity = NS::Core::Vector3{-awayX * launch, launch * m_launchUpScale, -awayZ * launch};
            m_didRebound = true;
            NS_LOG_INFO(Game,
                        "衝突: 質量 {} 耐久 {} 返り {} 押し飛ばし {} 中心近く {}",
                        mass,
                        hit->Toughness(),
                        rebound,
                        launch,
                        centerHit);
            stopSteps = ComputeHitStopSteps(power, mass, hitStopScale);
        }

        // 止めるフレーム数が決まってから控える。止めが 0 フレームの当たりも残すので、下の return より手前に置く
        m_lastImpact.sequence += 1;
        m_lastImpact.targetId = m_pendingTarget.id;
        m_lastImpact.power = power;
        m_lastImpact.charge01 = charge01;
        m_lastImpact.positionFactor = positionFactor;
        m_lastImpact.cameraShake = m_pendingShakeStrength;
        m_lastImpact.hitStopSteps = stopSteps;
        m_lastImpact.centerHit = centerHit;
        m_lastImpact.broke = m_pendingBreak;
        m_lastImpact.selfVelocity = m_pendingSelfVelocity;
        m_lastImpact.launchVelocity = m_pendingLaunchVelocity;
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

        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return;
        }

        // 力が伝わった瞬間の絵。凍結の頭で相手を発射方向へ食い込ませて止める。当たりは動かさない
        if (NS::Obj::GameObject* target = scene->Objects().FindObject(m_pendingTarget))
        {
            target->Root().SetPosition(m_pendingTargetHome + m_pendingImpactDir * m_pushInDistance);
        }

        if (NS::Obj::CameraBrain* brain = scene->CameraBrain())
        {
            brain->StartShake(m_pendingShakeStrength, stopSteps);
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
        m_centerHitFlashRemaining = 0;
    }

    void ImpactResolver::OnRenderOverlay(const NS::Gfx::RenderContext& ctx)
    {
        if (m_centerHitFlashRemaining <= 0)
        {
            return;
        }
        // ScreenFade は黒の固定色と暗転の段階機械で、白の瞬間減衰には流用できないためここで直接描く
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
        m_movement->SetVelocity(m_pendingSelfVelocity);
        if (m_scaleHeld)
        {
            // 解放の伸びが衝突の後半。伸びる軸は進行の軸と同じで、高さは戻して横だけ伸ばす
            m_stretchScale = ScaledAlongImpact(m_stretchAlong, 1.0f);
            RootTransform().SetScale(m_stretchScale);
            m_recoverRemaining = m_stretchRecoverSteps;
            m_scaleHeld = false;
        }

        const bool wasBreak = m_pendingBreak;
        m_pendingBreak = false;

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

        // 食い込みと振動は絵だけ。結果の起点がずれないよう元位置へ厳密に戻してから先へ進む
        target->Root().SetPosition(m_pendingTargetHome);

        // 跡は破壊と押し飛ばしの両方で出す。片方だけ何も残らないと結果が非対称になる
        bool floorFound = false;
        NS::Core::Vector3 markPosition{0.0f, 0.0f, 0.0f};
        {
            // 起点は相手の底の下。中心から始めると相手自身の当たりに 0 距離で当たる
            NS::Core::Vector3 probe = m_pendingTargetHome;
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
                markPosition = NS::Core::Vector3{m_pendingTargetHome.x, probe.y - dist + 0.02f, m_pendingTargetHome.z};
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

        body->Launch(m_pendingLaunchVelocity);
        if (floorFound)
        {
            (void)ImpactMark::SpawnAt(scene, markPosition);
        }
    }

    void ImpactResolver::ApplyFreezeVibration()
    {
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
        const float t = static_cast<float>(m_recoverRemaining) / static_cast<float>(m_stretchRecoverSteps);
        RootTransform().SetScale(m_scaleHome + (m_stretchScale - m_scaleHome) * t);
    }

    NS::Core::Vector3 ImpactResolver::ScaledAlongImpact(float along, float height) const noexcept
    {
        // 衝突は水平でしか起きない。進行の軸成分の 2 乗で倍率を混ぜ、軸に載った衝突では素の倍率になる
        const float dx2 = m_pendingImpactDir.x * m_pendingImpactDir.x;
        const float dz2 = m_pendingImpactDir.z * m_pendingImpactDir.z;
        return NS::Core::Vector3{m_scaleHome.x * (1.0f + (along - 1.0f) * dx2),
                                 m_scaleHome.y * height,
                                 m_scaleHome.z * (1.0f + (along - 1.0f) * dz2)};
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
