#include "Game/Level/ImpactResolver.h"

#include "Game/Level/CollisionInput.h"
#include "Game/Level/HitZones.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Player.h"
#include "Game/Player/LaunchPitch.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/IUse/IUseCamera.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/HitSensorDirector.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Physics/Capsule.h"
#include "Runtime/Platform/Clock.h"

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

        // 自機の位置から向きの線を引いた時の、相手の外接箱の中心の測り
        struct LineOffset
        {
            float along = 0.0f; // 自機の位置から相手の中心までの、線に沿った水平の距離 (m)。後ろは負
            float ratio = 0.0f; // 線から相手の中心までの横ずれ ÷ (相手の半幅 + 自機の半径)。0〜1 へ丸めない
        };

        // 狙う相手の絞りだけが使う。段と横ずれの値は裁定と同じく JudgeHitFace で出す
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
            return NS::Phys::Capsule{
                .center = origin + direction * half, .axis = direction, .halfHeight = half, .radius = radius};
        }

        // 玉が相手の体に初めて触れるまでに線に沿って進む距離 (m)。k_ContactTolerance の幅で、触れている側の端を返す
        // 掃く長さを伸ばすほど触れる形は増えるだけなので、触れない長さと触れる長さの間を半分ずつ詰める
        // 事前条件: SweptBall(origin, direction, distance, radius) が target に触れている
        [[nodiscard]] float FirstTouchDistance(const NS::Obj::SensorVolume& target,
                                               const NS::Core::Vector3& origin,
                                               const NS::Core::Vector3& direction,
                                               float distance,
                                               float radius) noexcept
        {
            const auto touches = [&](float length) {
                return NS::Obj::VolumesOverlap(
                    NS::Obj::SensorVolume::Capsule(SweptBall(origin, direction, length, radius)), target);
            };
            if (touches(0.0f))
            {
                return 0.0f;
            }
            float missed = 0.0f;
            float touched = distance;
            for (int i = 0; i < k_ContactSearchSteps && touched - missed > k_ContactTolerance; ++i)
            {
                const float middle = (missed + touched) * 0.5f;
                if (touches(middle))
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

        // この固定ステップで進んだ先の根の位置。重なりと段を同じ所で見るため、裁定はどちらもここを通る
        [[nodiscard]] NS::Core::Vector3 PositionAfterStep(const NS::Core::Vector3& position,
                                                          const NS::Core::Vector3& velocity) noexcept
        {
            const float dt = NS::Platform::FrameTimer::FixedDelta();
            return NS::Core::Vector3{
                position.x + velocity.x * dt, position.y + velocity.y * dt, position.z + velocity.z * dt};
        }

        // 体当たりが調べる種類の、有効な体のセンサーか。当たりの裁定と狙う相手の探索が同じ絞りを通る
        [[nodiscard]] bool IsTackleTarget(const NS::Obj::HitSensor& sensor, const NS::Obj::Actor* self) noexcept
        {
            return sensor.IsValid() && sensor.Owner() != self &&
                   NS::Obj::HitSensorDirector::Checks(NS::Obj::HitSensorType::PlayerAttack, sensor.Type());
        }
    } // namespace

    // 身体の移動より前。書き込んだ速度が同じ固定ステップの移動に乗る
    ImpactResolver::ImpactResolver() noexcept : NS::Obj::Component() {}

    const NS::Game::Player::PlayerParams& ImpactResolver::Tuning() const noexcept
    {
        if (const ::Player* ownerPlayer = NS::Obj::Cast<::Player>(Owner()))
        {
            return ownerPlayer->Params();
        }
        static const NS::Game::Player::PlayerParams defaults;
        return defaults;
    }

    void ImpactResolver::OnStart()
    {
        if (::Player* ownerPlayer = NS::Obj::Cast<::Player>(Owner()))
        {
            m_player = ownerPlayer;
            m_body = &ownerPlayer->Body();
            m_collisionInput = &ownerPlayer->ChargeControl();
            m_hitReaction = ownerPlayer->HitReactionPart();
        }
    }

    int ImpactResolver::CenterHitFlashStepsRemaining() const noexcept
    {
        if (m_hitReaction == nullptr)
        {
            return 0;
        }
        return m_hitReaction->FlashFramesRemaining();
    }

    NS::Obj::HitSensor* ImpactResolver::FindOverlapped(const NS::Core::Vector3& predictedVelocity) const
    {
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return nullptr;
        }

        const NS::Core::Vector3 position = Owner()->Root().Position();

        // この固定ステップで進んだ先で見る。今の位置だけでは手前で止められて重ならず、反発が起きない
        const NS::Phys::Capsule capsule = m_body->CapsuleAt(PositionAfterStep(position, predictedVelocity));
        const std::vector<NS::Obj::HitSensor*> touching = scene->HitSensors().FindOverlaps(
            NS::Obj::SensorVolume::Capsule(capsule), NS::Obj::HitSensorType::PlayerAttack, Owner());

        NS::Obj::HitSensor* nearest = nullptr;
        float nearestDistanceSq = 0.0f;
        for (NS::Obj::HitSensor* sensor : touching)
        {
            const NS::Core::AABB bounds = sensor->WorldVolume().Bounds();
            const float dx = bounds.Center.x - position.x;
            const float dy = bounds.Center.y - position.y;
            const float dz = bounds.Center.z - position.z;
            const float distanceSq = dx * dx + dy * dy + dz * dz;
            if (nearest == nullptr || distanceSq < nearestDistanceSq)
            {
                nearest = sensor;
                nearestDistanceSq = distanceSq;
            }
        }
        return nearest;
    }

    bool ImpactResolver::FindSlamLineTarget(const NS::Core::Vector3& direction,
                                            float maxDistance,
                                            SlamLineTarget& outTarget) const
    {
        NS::Core::Vector3 lineDir{};
        if (Owner() == nullptr || m_body == nullptr || !NS::Core::TryNormalizeHorizontal(direction, lineDir))
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
        const float playerRadius = m_body->CapsuleRadius();
        // 突進は丸まった玉で進む。玉の決まりは Player::SlamBallAt が持つ
        const NS::Core::Vector3 ballCenter = m_player->SlamBallAt(position).center;
        // 届くかは裁定と同じく、自機の当たりの玉と相手の体のセンサーの形で見る。外接箱を水平に見ると、中心の高い
        // 大きな球の端では、玉が触れずに横を通るのに届くと出る
        const NS::Obj::SensorVolume swept =
            NS::Obj::SensorVolume::Capsule(SweptBall(ballCenter, lineDir, maxDistance, playerRadius));

        // TODO: 体のセンサーを総当たりで見ている。数十個までを想定。増えたら格子で絞る
        bool found = false;
        SlamLineTarget first{};
        NS::Obj::HitSensor* firstSensor = nullptr;
        for (NS::Obj::HitSensor* sensor : scene->HitSensors().Sensors())
        {
            if (!IsTackleTarget(*sensor, Owner()))
            {
                continue;
            }
            const NS::Obj::SensorVolume volume = sensor->WorldVolume();
            const NS::Core::AABB bounds = volume.Bounds();

            const LineOffset line = MeasureLineOffset(position, bounds, lineDir, playerRadius);
            // 真横と後ろの相手は線の先に居ない
            if (!(line.along > 0.0f))
            {
                continue;
            }
            // 比が 1 を超える相手は、線を進む自機の縁が相手の外接箱の縁に届かない。丸めた値で見ると 1 に張り付いて拾う
            if (!(line.ratio <= 1.0f))
            {
                continue;
            }
            if (!NS::Obj::VolumesOverlap(swept, volume))
            {
                continue;
            }
            // 最初に触れる相手は、中心の近さでなく玉が触れるまでに進む距離で決める。突進はそこで止まって当たる
            const float contact = FirstTouchDistance(volume, ballCenter, lineDir, maxDistance, playerRadius);
            if (!found || contact < first.contact)
            {
                found = true;
                first = SlamLineTarget{.target = NS::Obj::ActorRef{sensor->Owner()->Id()},
                                       .bounds = bounds,
                                       .origin = position,
                                       .direction = lineDir,
                                       .along = line.along,
                                       .contact = contact};
                firstSensor = sensor;
            }
        }
        if (!found)
        {
            return false;
        }

        // 応じない相手は裁定でも当たらないので、外れの既定のまま水平に放つ
        first.launchContact = first.contact;
        HitFaceJudgement judgement;
        TackleTargetAnswer answer{};
        if (SendMsgAskTackleTarget(*firstSensor, answer))
        {
            const NS::Game::Player::PlayerParams& params = Tuning();
            float aimHeight = ballCenter.y;
            if (!HitFaceAimHeight(answer.face, answer.body, lineDir, playerRadius, aimHeight))
            {
                aimHeight = ballCenter.y;
            }
            // 触れる所は、着きたい高さで線を進めた玉が触れる所。今の高さで測ると、高さの違う相手の上の縁をかすめる
            // 所まで寄ってしまい、弧の着く所と実際に触れる所がずれる。その高さで触れない時は今の高さの値のまま
            const NS::Core::Vector3 aimedCenter{ballCenter.x, aimHeight, ballCenter.z};
            float aimedContact = first.contact;
            if (NS::Obj::VolumesOverlap(
                    NS::Obj::SensorVolume::Capsule(SweptBall(aimedCenter, lineDir, maxDistance, playerRadius)),
                    firstSensor->WorldVolume()))
            {
                aimedContact =
                    FirstTouchDistance(firstSensor->WorldVolume(), aimedCenter, lineDir, maxDistance, playerRadius);
            }
            const float dt = NS::Platform::FrameTimer::FixedDelta();
            const NS::Game::Player::LaunchPitchResult pitch = NS::Game::Player::LaunchPitch(
                NS::Game::Player::LaunchPitchDesc{.ballHeight = ballCenter.y,
                                                  .targetHeight = aimHeight,
                                                  .contactDistance = aimedContact,
                                                  .horizontalSpeed = params.m_bodySlamSpeed,
                                                  .gravity = params.Gravity(),
                                                  .maxAngleDegrees = params.m_launchPitchLimitDegrees,
                                                  .grounded = m_body->IsGrounded(),
                                                  .dt = dt});
            first.launchVerticalSpeed = pitch.verticalSpeed;
            if (pitch.reachable)
            {
                first.launchContact = aimedContact;
            }
            // 段と横ずれは裁定と同じく相手の答えの面で決める。玉の高さは放つ縦の速さの道筋が触れる所で居る高さ
            const NS::Game::Player::LaunchPath path{.horizontalSpeed = params.m_bodySlamSpeed,
                                                    .verticalSpeed = pitch.verticalSpeed,
                                                    .gravity = params.Gravity(),
                                                    .dt = dt,
                                                    .grounded = m_body->IsGrounded()};
            const NS::Core::Vector3 arrival{
                ballCenter.x, ballCenter.y + NS::Game::Player::LaunchHeightAt(path, first.launchContact), ballCenter.z};
            judgement = JudgeHitFaceOrWide(answer.face, answer.body, arrival, lineDir, playerRadius);
        }
        first.offset = judgement.offset01;
        first.tier = judgement.tier;
        outTarget = first;
        return true;
    }

    void ImpactResolver::OnUpdate()
    {
        NS::Core::Vector3 velocity{};
        if (m_body != nullptr)
        {
            velocity = m_player->BodySlamVelocity();
        }
        ObserveImpact(velocity);
        StepState();
    }

    void ImpactResolver::ObserveImpact(const NS::Core::Vector3& predictedVelocity)
    {
        m_stateReady = true;
        m_hasObservedTarget = false;
        if (m_body == nullptr || !m_player->IsBodySlamming() || m_hitStopRemaining > 0 || m_freezePendingSteps > 0)
        {
            return;
        }
        NS::Obj::HitSensor* hit = FindOverlapped(predictedVelocity);
        if (hit == nullptr || hit->Owner() == nullptr)
        {
            return;
        }
        const NS::Obj::ActorRef target{hit->Owner()->Id()};
        TackleTargetAnswer answer{};
        if (!SendMsgAskTackleTarget(*hit, answer))
        {
            return;
        }
        m_observedTarget = target;
        m_observedAnswer = answer;
        m_observedVelocity = predictedVelocity;
        m_hasObservedTarget = true;
    }

    void ImpactResolver::StepState()
    {
        if (!m_stateReady)
        {
            return;
        }
        m_stateReady = false;
        const bool hadObservation = m_hasObservedTarget;
        m_hasObservedTarget = false;
        m_didRebound = false;
        m_didBreak = false;
        m_freezeBeganThisStep = false;
        m_releasedThisStep = false;
        // 白の光と振動の進みは HitReaction が持つ。止まっている間も薄れる
        if (m_body == nullptr)
        {
            return;
        }

        // 止まっている間は新しい衝突を見ない。凍った自機は重なったままなので、見ると毎フレーム検知し直す
        if (m_hitStopRemaining > 0)
        {
            --m_hitStopRemaining;
            if (m_hitStopRemaining == 0)
            {
                // 止めが 0 の当たりが検知のフレームで呼ぶ ReleaseHitStop は明けに数えない。立てるのはこの道だけ
                m_releasedThisStep = true;
                ReleaseHitStop();
                return;
            }
            // 置かれていた相手の往復は相手が自分で置く
            return;
        }

        if (m_freezePendingSteps > 0)
        {
            m_freezeBeganThisStep = true;
            BeginFreeze(m_freezePendingSteps);
            m_freezePendingSteps = 0;
            return;
        }

        if (m_recoverRemaining > 0)
        {
            RecoverScale();
        }

        // 押していない接触は物理の停止だけで済ませるため、体当たり中でないフレームは裁定しない
        if (!m_player->IsBodySlamming())
        {
            return;
        }

        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (!hadObservation || scene == nullptr)
        {
            return;
        }
        const NS::Obj::Actor* target = scene->Objects().FindObject(m_observedTarget);
        if (target == nullptr || !target->IsActiveInHierarchy())
        {
            return;
        }
        const TackleTargetAnswer& answer = m_observedAnswer;
        const NS::Core::AABB bounds = answer.bounds;

        const NS::Core::Vector3 position = Owner()->Root().Position();
        // 箱へ押し付けられたフレームは実速度が 0 に潰されるため、突進の狙いの速度で向きと貫通後の速度を決める
        const NS::Core::Vector3 velocity = m_observedVelocity;

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

        const float charge01 = m_player->BodySlamCharge01();

        const float mass = answer.mass;

        // 段と威力の当たり位置の係数は、相手の面で当てはまった同じ決まりから取る
        // 玉の中心は重なりを見た所と同じく、この固定ステップで進んだ先。今の位置で見ると、縦に動く突進は 1 ステップ
        // ぶん違う高さで段が決まる。玉は狙う相手の探し方と同じ Player::SlamBallAt から引く
        const NS::Core::Vector3 stepped = PositionAfterStep(position, velocity);
        const NS::Core::Vector3 ballCenter = m_player->SlamBallAt(stepped).center;
        const HitFaceJudgement judgement =
            JudgeHitFaceOrWide(answer.face, answer.body, ballCenter, velocity, m_body->CapsuleRadius());
        const float offset01 = judgement.offset01;
        // ボタン未搭載は係数 1.0 の素通し。段は中心近くと記録するが、段の返りと止めと反動の距離の倍率は掛けない
        // 外し方は段の種類に混ぜず、下の tiered で分ける
        float chargeFactor = 1.0f;
        float positionFactor = 1.0f;
        HitTier tier = HitTier::Center;
        const bool tiered = m_collisionInput != nullptr;
        if (tiered)
        {
            chargeFactor = m_collisionInput->ChargeFactorFor(charge01);
            positionFactor = judgement.powerScale;
            tier = judgement.tier;
        }
        // 読むのは記録と WasCenterHit とログだけ。配分と返りは段で分ける
        const bool centerHit = tiered && tier == HitTier::Center;
        // 最終威力 = チャージ倍率 × 当たり位置係数。破壊の判定だけでなく反発・発射・揺れも威力で作る
        const float power = chargeFactor * positionFactor;
        m_lastCharge01 = charge01;
        m_lastPositionFactor = positionFactor;
        m_lastPower = power;
        m_wasCenterHit = centerHit;
        NS_LOG_INFO(Game,
                    "威力の内訳: 溜め {} × 当たり位置 {} = {} 溜め量 {} 中心からの横ずれ {}",
                    chargeFactor,
                    positionFactor,
                    power,
                    charge01,
                    offset01);

        // 明けたフレームの反発と貫通速度を通常移動に乗せるため、凍結より先に突進を打ち切る
        m_player->CancelBodySlam();

        m_pendingTarget = m_observedTarget;
        m_pendingTargetHome = answer.position;
        // 飛んでいる相手は食い込まない。どう応じるかは相手が決めるが、演出の大きさを選ぶのに答えを控える
        m_pendingTargetPlaced = answer.placed;
        m_pendingTier = tier;
        // 相手は突進の向きへ飛ばす。中心の並びで飛ばすと、横ずれのある当たりが狙いと別の所へ飛ぶ
        // 突進の水平の速さがほぼ 0 で向きが決まらない時だけ、中心の並びの向きへ飛ばす
        NS::Core::Vector3 launchDir{-awayX, 0.0f, -awayZ};
        NS::Core::Vector3 slamDir{};
        if (NS::Core::TryNormalizeHorizontal(velocity, slamDir))
        {
            launchDir = slamDir;
        }
        m_pendingImpactDir = launchDir;
        // TODO: 敵が自機に当たる場面が出たら、裁定をシーンに 1 つの物へ移す。今は自機だけが検知する
        ImpactInput impactInput;
        impactInput.power = power;
        impactInput.tier = tier;
        impactInput.mass = mass;
        impactInput.toughness = answer.toughness;
        impactInput.breakable = answer.breakable;
        impactInput.awayDirection = NS::Core::Vector3{awayX, 0.0f, awayZ};
        impactInput.launchDirection = launchDir;
        impactInput.slamVelocity = velocity;
        ImpactTuning impactTuning = MakeImpactTuning(Tuning());
        if (!tiered)
        {
            impactTuning.centerHitStopScale = 1.0f;
            impactTuning.centerHitReboundDistanceScale = 1.0f;
        }
        const ImpactOutcome outcome = ComputeImpactOutcome(impactInput, impactTuning);
        m_pendingShakeAmplitude = outcome.shakeAmplitude;

        const float reboundScale = outcome.reboundScale;
        const float launchScale = outcome.launchScale;
        const int stopSteps = outcome.stopSteps;
        if (outcome.broke)
        {
            m_pendingBreak = true;
            // 貫通は相手を飛ばさず、自機も反動しない
            // 前の当たりの曲線を残すと、この当たりの記録に使っていない曲線が載る
            m_pendingLaunchArc = outcome.launchArc;
            m_pendingReboundArc = outcome.reboundArc;
            m_pendingSelfVelocity = outcome.breakSelfVelocity;
            m_didBreak = true;
            NS_LOG_INFO(Game, "貫通: 耐久 {} 威力 {} 中心近く {}", answer.toughness, power, centerHit);
        }
        else
        {
            m_pendingBreak = false;
            m_pendingReboundArc = outcome.reboundArc;
            m_pendingSelfVelocity = m_player->ReboundVelocityFor(m_pendingReboundArc);
            m_pendingLaunchArc = outcome.launchArc;

            m_didRebound = true;
            NS_LOG_INFO(Game,
                        "衝突: 質量 {} 耐久 {} 反動の高さ {} 距離 {} 押し飛ばしの距離 {} 高さ {} 中心近く {}",
                        mass,
                        answer.toughness,
                        m_pendingReboundArc.apexHeight,
                        m_pendingReboundArc.distance,
                        m_pendingLaunchArc.distance,
                        m_pendingLaunchArc.apexHeight,
                        centerHit);
        }

        PrepareHitReturns(tier, tiered, power, outcome.massFactor, offset01, stopSteps);

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
        // 触れた点は記録とエディタの印だけが読む。段と威力を決めた判定の結果を使う
        m_lastImpact.surfacePoint = judgement.surfacePoint;
        m_lastImpact.targetPos = m_pendingTargetHome;
        m_lastImpact.targetBottom = bounds.Center.y - bounds.Extents.y;
        m_lastImpact.targetMass = mass;
        m_lastImpact.targetPlaced = m_pendingTargetPlaced;
        m_lastImpact.launchScale = launchScale;
        m_pendingLaunchScale = launchScale;
        m_lastImpact.reboundScale = reboundScale;

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
        // 自機を寝かせて凍らせる。Player の StateStep と BodyStep はこの後に身体の active を見るので、
        // 同じフレームから効く
        m_hitStopRemaining = stopSteps;
        m_hitStopTotal = stopSteps;
        m_body->SetActive(false);
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
            RootTransform().SetScale(ScaledAlongImpact(Tuning().m_squashThickness, Tuning().m_squashHeight));
        }

        StartHitReturns();

        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return;
        }

        // 力が伝わった瞬間の絵。相手へ止めの頭を知らせ、置かれていれば食い込ませて止めさせる
        // 相手も自機と同じく、押し返されている反発の時だけ縮める
        if (NS::Obj::Actor* target = scene->Objects().FindObject(m_pendingTarget))
        {
            const TackleFreezeDesc freeze{.impactDir = m_pendingImpactDir,
                                          .pushInDistance = Tuning().m_pushInDistance,
                                          .shakeAmplitude = m_pendingShakeAmplitude,
                                          .squashThickness = Tuning().m_squashThickness,
                                          .squashHeight = Tuning().m_squashHeight,
                                          .squash = !m_pendingBreak,
                                          .stopSteps = stopSteps};
            (void)SendMsgTackleFreeze(*target, freeze);
        }
    }

    ImpactResolver::TierReturns ImpactResolver::PlainReturns(float swing, int stopSteps) noexcept
    {
        TierReturns returns;
        returns.shake.upAmplitude = swing;
        returns.shake.frames = stopSteps;
        returns.shake.longestFlipFrames = 1;
        return returns;
    }

    ImpactResolver::TierReturns ImpactResolver::TierReturnsFor(HitTier tier, float swing, int stopSteps) const noexcept
    {
        // 振動は段ごとにモーターを分ける。強さは質量と威力で変えない
        switch (tier)
        {
        case HitTier::Center:
        {
            // 縦だけを毎フレーム入れ替え、寄り・傾き・振動の長さを止めで結ぶ
            TierReturns returns = PlainReturns(swing * Tuning().m_centerHitShakeScale, stopSteps);
            returns.flashSteps = Tuning().m_centerHitFlashSteps;
            returns.zoomRoll.zoom = Tuning().m_centerHitZoom;
            returns.zoomRoll.rollDegrees = Tuning().m_centerHitRollDegrees;
            returns.zoomRoll.holdFrames = stopSteps;
            returns.zoomRoll.returnFrames = Tuning().m_zoomRollReturnFrames;
            returns.pad.start.left = Tuning().m_centerHitPadStrength;
            returns.pad.fadeFrames = stopSteps;
            returns.pad.frames = stopSteps;
            return returns;
        }
        case HitTier::Wide:
        {
            TierReturns returns;
            // 横と縦を合わせた長さが最初の振れの大きさになるよう、比で分ける
            const float upOverSide = Tuning().m_wideShakeUpOverSide;
            const float side = swing / std::sqrt(1.0f + upOverSide * upOverSide);
            returns.shake.sideAmplitude = side;
            returns.shake.upAmplitude = side * upOverSide;
            returns.shake.frames = Tuning().m_wideShakeFrames;
            returns.shake.longestFlipFrames = Tuning().m_wideShakeLongestFlipFrames;
            returns.pad.start.right = Tuning().m_widePadStrength;
            returns.pad.fadeFrames = Tuning().m_wideShakeFrames;
            returns.pad.frames = Tuning().m_wideShakeFrames;
            return returns;
        }
        }
        // 番号から作った段の外の値は段の返りを掛けない
        return PlainReturns(swing, stopSteps);
    }

    void ImpactResolver::PrepareHitReturns(
        HitTier tier, bool tiered, float power, float massFactor, float offset01, int stopSteps)
    {
        // 最初の振れの大きさは全段で同じ式。反発と同じ質量因子を掛け、段の倍率は表の行が掛ける
        const float swing = Tuning().m_cameraShakeScale * power * massFactor;
        // 段の無い台は段の表を引かず、白と寄りと傾きと振動を出さない
        TierReturns returns = PlainReturns(swing, stopSteps);
        if (tiered)
        {
            returns = TierReturnsFor(tier, swing, stopSteps);
        }

        m_pendingFlashSteps = returns.flashSteps;
        m_pendingShake = returns.shake;
        m_pendingShake.firstSideDirection = m_pendingReboundArc.direction;
        m_pendingShake.seed = ShakeSeed(m_pendingTarget.id, offset01, m_pendingImpactDir, m_pendingTargetHome);
        // 寄りの無い段も倍率 1 の設定を渡し、前の当たりの寄りを残さない
        m_pendingZoomRoll = returns.zoomRoll;
        m_pendingZoomRoll.rollDirection = m_pendingImpactDir;
        m_pendingPad = returns.pad;

        // 当たりの記録は検知のフレームに読まれるので、傾きの向きもここで今のカメラから決める
        const float rollSign = NS::Obj::CameraSideSignOf(*Owner(), m_pendingZoomRoll.rollDirection);
        m_lastImpact.cameraShake = NS::Core::Vector2{m_pendingShake.sideAmplitude, m_pendingShake.upAmplitude}.Length();
        m_lastImpact.flashStart = m_pendingFlashSteps;
        m_lastImpact.zoomStart = m_pendingZoomRoll.zoom;
        m_lastImpact.rollStart = m_pendingZoomRoll.rollDegrees * rollSign;
        m_lastImpact.padStart = m_pendingPad.start;
    }

    void ImpactResolver::StartHitReturns()
    {
        if (m_hitReaction == nullptr)
        {
            return;
        }
        const NS::Obj::HitReactionDesc reaction{.flashFrames = m_pendingFlashSteps,
                                                .flashAlpha = Tuning().m_centerHitFlashAlpha,
                                                .shake = m_pendingShake,
                                                .zoomRoll = m_pendingZoomRoll,
                                                .pad = m_pendingPad};
        m_hitReaction->Play(reaction);
    }

    void ImpactResolver::OnEndPlay()
    {
        m_stateReady = false;
        m_hasObservedTarget = false;
        // 凍結の途中で裁定が外れても、移動が止まったまま残らないようにする
        // 白と振動とカメラの効果は HitReaction が自分の OnEndPlay で止める
        if (m_body != nullptr)
        {
            m_body->SetActive(true);
        }
    }

    ImpactTuning MakeImpactTuning(const NS::Game::Player::PlayerParams& params) noexcept
    {
        return ImpactTuning{.centerHitStopScale = params.m_centerHitStopScale,
                            .shakeAmplitude = params.m_shakeAmplitude,
                            .breakEnabled = params.m_breakEnabled,
                            .breakSpeedScale = params.m_breakSpeedScale,
                            .breakStopSeconds = params.m_breakStopSeconds,
                            .reboundDistance = params.m_reboundDistance,
                            .reboundApexHeight = params.m_reboundApexHeight,
                            .centerHitReboundDistanceScale = params.m_centerHitReboundDistanceScale,
                            .launchDistance = params.m_launchDistance,
                            .launchMassExponent = params.m_launchMassExponent,
                            .launchApexHeight = params.m_launchApexHeight,
                            .launchRiseGravity = params.m_launchRiseGravity,
                            .launchFallGravityScale = params.m_launchFallGravityScale,
                            .launchApexBandSpeed = params.m_launchApexBandSpeed,
                            .launchApexBandGravityScale = params.m_launchApexBandGravityScale,
                            .hitStopBaseSeconds = params.m_hitStopBaseSeconds,
                            .hitStopMaxSeconds = params.m_hitStopMaxSeconds,
                            .fixedDelta = NS::Platform::FrameTimer::FixedDelta()};
    }

    void ImpactResolver::ReleaseHitStop()
    {
        m_body->SetActive(true);
        const bool wasBreak = m_pendingBreak;
        m_pendingBreak = false;
        if (wasBreak)
        {
            m_body->SetVelocity(m_pendingSelfVelocity);
        }
        else if (!m_player->BeginRebound(m_pendingReboundArc))
        {
            // 欄が曲線にならない値の時だけ通る。書かないと、止める前の最後のフレームの速度のまま動き出す
            m_body->SetVelocity(m_pendingSelfVelocity);
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
                m_stretchScale = ScaledAlongImpact(Tuning().m_stretchAlong, 1.0f);
            }
            else
            {
                m_stretchScale =
                    NS::Core::Vector3{m_scaleHome.x, m_scaleHome.y * Tuning().m_stretchAlong, m_scaleHome.z};
            }
            RootTransform().SetScale(m_stretchScale);
            m_recoverRemaining = Tuning().m_stretchRecoverSteps;
            m_scaleHeld = false;
        }
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return;
        }

        // 相手は id で引き直す。止まっている数フレームの間に消されていたら残りだけ諦める
        // 元の位置と形へ戻り、跡を残し、飛ぶか壊れるかは相手が決める
        NS::Obj::Actor* target = scene->Objects().FindObject(m_pendingTarget);
        if (target == nullptr)
        {
            return;
        }
        const TackleReleaseDesc release{.arc = m_pendingLaunchArc,
                                        .breaks = wasBreak,
                                        .tier = m_pendingTier,
                                        .power = m_lastPower,
                                        .launchScale = m_pendingLaunchScale};
        (void)SendMsgTackleRelease(*target, release);
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
        const float total = static_cast<float>(Tuning().m_stretchRecoverSteps);
        const float half = total * 0.5f;
        const float elapsed = total - static_cast<float>(m_recoverRemaining);
        const NS::Core::Vector3 overshoot = m_scaleHome - (m_stretchScale - m_scaleHome) * Tuning().m_stretchOvershoot;
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

    NS_CLASS(ImpactResolver)
} // namespace NS::Game::Level
