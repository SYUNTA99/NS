#include "Game/Level/ImpactResolver.h"

#include "Game/Level/HitZones.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "Game/Player.h"
#include "Game/Player/ImpactEffects.h"
#include "Game/Player/LaunchPitch.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/Collider.h"
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
#include <utility>
#include <variant>
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

        // 曲線の倍率。点の無い曲線は元の形の 1。0 を返すと描く形が消える
        [[nodiscard]] float CurveFactor(const NS::Obj::Curve& curve, float frame) noexcept
        {
            if (curve.count == 0)
            {
                return 1.0f;
            }
            return curve.Evaluate(frame);
        }

        // 止めの事象の長さ。貫通の当たりは breakStopSteps (0 以上) に置き換える
        [[nodiscard]] int StopLengthOf(const HitEvent& event, int breakStopSteps) noexcept
        {
            if (breakStopSteps >= 0)
            {
                return breakStopSteps;
            }
            return event.length;
        }

        // 当たりの記録へ書く止めのフレーム数。最初の止めの事象の長さで、止めの事象が無ければ 0
        [[nodiscard]] int StopStepsOf(const std::vector<HitEvent>& events, int breakStopSteps) noexcept
        {
            for (const HitEvent& event : events)
            {
                if (std::holds_alternative<HitStopEvent>(event.value))
                {
                    return std::max(StopLengthOf(event, breakStopSteps), 0);
                }
            }
            return 0;
        }

        // timeline のうち、向き direction の当たりで起きる事象。並びの順は保つ
        [[nodiscard]] std::vector<HitEvent> EventsFor(const HitTimeline& timeline, HitDirection direction)
        {
            std::vector<HitEvent> events;
            events.reserve(timeline.events.size());
            for (const HitEvent& event : timeline.events)
            {
                if (event.direction == HitDirection::Any || event.direction == direction)
                {
                    events.push_back(event);
                }
            }
            return events;
        }

        // 体当たりの相手を決める唯一の所。置物の体だけを相手にする
        // 有効か・自分かの絞りは FindOverlaps が持つ。当たりの裁定と狙う相手の探索が同じ絞りを通る
        [[nodiscard]] bool IsTackleTarget(const NS::Obj::HitSensor& sensor) noexcept
        {
            return IsSensorKind(sensor, SensorKind::MapObjBody);
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
        const NS::Phys::Capsule capsule =
            m_player->Collider().CapsuleAt(PositionAfterStep(position, predictedVelocity));
        const std::vector<NS::Obj::HitSensor*> touching =
            scene->HitSensors().FindOverlaps(NS::Obj::SensorVolume::Capsule(capsule), Owner());

        NS::Obj::HitSensor* nearest = nullptr;
        float nearestDistanceSq = 0.0f;
        for (NS::Obj::HitSensor* sensor : touching)
        {
            if (!IsTackleTarget(*sensor))
            {
                continue;
            }
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
        const float playerRadius = m_player->Collider().CapsuleRadius();
        // 突進は丸まった玉で進む。玉の決まりは Player::SlamBallAt が持つ
        const NS::Core::Vector3 ballCenter = m_player->SlamBallAt(position).center;
        // 届くかは裁定と同じく、自機の当たりの玉と相手の体のセンサーの形で見る。外接箱を水平に見ると、中心の高い
        // 大きな球の端では、玉が触れずに横を通るのに届くと出る
        const NS::Obj::SensorVolume swept =
            NS::Obj::SensorVolume::Capsule(SweptBall(ballCenter, lineDir, maxDistance, playerRadius));

        bool found = false;
        SlamLineTarget first{};
        NS::Obj::HitSensor* firstSensor = nullptr;
        // 掃いた玉に触れる相手だけを先に問う。下の線の条件はどれも順に依らないので、選ぶ相手は絞る順で変わらない
        for (NS::Obj::HitSensor* sensor : scene->HitSensors().FindOverlaps(swept, Owner()))
        {
            if (!IsTackleTarget(*sensor))
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

    void ImpactResolver::ObserveImpact()
    {
        m_stateReady = true;
        m_hasObservedTarget = false;
        if (m_body == nullptr || !m_player->IsBodySlamming() || IsHoldingPlayer())
        {
            return;
        }
        const NS::Core::Vector3 predictedVelocity = m_player->BodySlamVelocity();
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

        if (m_clockRunning)
        {
            ++m_clock;
            AdvanceTimeline();
        }
        // 止めた自機を動かし直すまでは新しい衝突を見ない。止まった自機は重なったままなので、見ると毎フレーム検知し直す
        if (IsHoldingPlayer())
        {
            return;
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
            JudgeHitFaceOrWide(answer.face, answer.body, ballCenter, velocity, m_player->Collider().CapsuleRadius());
        const float offset01 = judgement.offset01;
        const float chargeFactor = Tuning().ChargeFactorFor(charge01);
        const float positionFactor = judgement.powerScale;
        const HitTier tier = judgement.tier;
        // 読むのは記録と WasCenterHit とログだけ。配分と返りは段で分ける
        const bool centerHit = tier == HitTier::Center;
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
        m_pendingTargetPosition = answer.position;
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
        const ImpactOutcome outcome = ComputeImpactOutcome(impactInput, MakeImpactTuning(Tuning()));
        m_pendingTargetMass = mass;

        const float reboundScale = outcome.reboundScale;
        const float launchScale = outcome.launchScale;
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

        // 前の当たりの事象が走っていれば、この当たりの事象を起こす前に止める。始めた返りも止め、
        // この当たりのタイムラインに置いた返りだけが出る
        if (m_clockRunning && m_hitReaction != nullptr)
        {
            m_hitReaction->Stop();
        }
        AbortTimeline();
        const HitDirection direction = HitDirectionOf(judgement.u, judgement.v);
        const HitTimeline* timeline = HitTimelineLibrary::Get().FindForTier(tier);
        std::vector<HitEvent> events;
        if (timeline != nullptr)
        {
            events = EventsFor(*timeline, direction);
        }
        // 貫通の止めはタイムラインへ移さず、欄「貫通の止め秒」の長さのまま
        int breakStopSteps = -1;
        if (outcome.broke)
        {
            breakStopSteps = outcome.stopSteps;
        }
        const int stopSteps = StopStepsOf(events, breakStopSteps);

        m_pendingPower = power;
        m_pendingMassFactor = outcome.massFactor;
        m_pendingShakeSeed = ShakeSeed(m_pendingTarget.id, offset01, m_pendingImpactDir, m_pendingTargetPosition);
        RecordReturns(events);

        // 止めるフレーム数が決まってから控える。タイムラインの引けない当たりも残すので、下の return より手前に置く
        m_lastImpact.sequence += 1;
        m_lastImpact.targetId = m_pendingTarget.id;
        m_lastImpact.power = power;
        m_lastImpact.charge01 = charge01;
        m_lastImpact.positionFactor = positionFactor;
        m_lastImpact.offset01 = offset01;
        m_lastImpact.tier = tier;
        m_lastImpact.direction = direction;
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
        m_lastImpact.targetPos = m_pendingTargetPosition;
        m_lastImpact.targetBottom = bounds.Center.y - bounds.Extents.y;
        m_lastImpact.targetMass = mass;
        m_lastImpact.targetPlaced = m_pendingTargetPlaced;
        m_lastImpact.launchScale = launchScale;
        m_pendingLaunchScale = launchScale;
        m_lastImpact.reboundScale = reboundScale;

        // 遊びの結果は配分で決まっているので、返りを置けない当たりも反動と飛ばしだけは出す
        if (timeline == nullptr)
        {
            ApplyRebound();
            LaunchTarget();
            return;
        }
        StartTimeline(std::move(events), breakStopSteps);
    }

    // 事象の種類ごとの受け持ち。種類を足して受け持ちを書き忘れると、std::visit が呼べずにコンパイルが止まる
    struct ImpactResolver::EventRunner
    {
        ImpactResolver& resolver;
        const HitEvent& event;

        void operator()(const HitStopEvent&) const
        {
            int length = event.length;
            if (resolver.m_breakStopSteps >= 0)
            {
                length = resolver.m_breakStopSteps;
            }
            if (length <= 0)
            {
                return;
            }
            // Player の StateStep と BodyStep はこの後に CanMoveBody で止めを見るので、同じフレームから効く
            resolver.m_stopStarted = true;
            resolver.m_stopEnd = resolver.m_clock + length - 1;
            resolver.m_freezeBeganThisStep = true;
            NS_LOG_INFO(Game, "ヒットストップ: {} フレーム", length);
        }

        void operator()(const ShapeEvent& shape) const
        {
            // 貫通は押し勝っている側なので形を変えない
            if (resolver.m_pendingBreak)
            {
                return;
            }
            resolver.m_shape = shape;
            resolver.m_shapeStart = resolver.m_clock;
            resolver.m_shapeLength = event.length;
            resolver.m_shapeActive = true;
        }

        void operator()(const TargetFreezeEvent& freeze) const
        {
            NS::Obj::Scene* scene = resolver.Owner()->OwningScene();
            if (scene == nullptr)
            {
                return;
            }
            // 力が伝わった瞬間の絵。置かれていれば食い込ませて止めさせ、押し返されている反発の時だけ縮める
            // 動きは軽い側が受け取るので、往復は重い相手ほど小さい
            NS::Obj::Actor* target = scene->Objects().FindObject(resolver.m_pendingTarget);
            if (target == nullptr)
            {
                return;
            }
            const TackleFreezeDesc desc{.impactDir = resolver.m_pendingImpactDir,
                                        .pushInDistance = freeze.pushInDistance,
                                        .shakeAmplitude = freeze.swingAmplitude / (1.0f + resolver.m_pendingTargetMass),
                                        .squashThickness = freeze.squashThickness,
                                        .squashHeight = freeze.squashHeight,
                                        .squash = !resolver.m_pendingBreak,
                                        .stopSteps = event.length};
            (void)SendMsgTackleFreeze(*target, desc);
        }

        void operator()(const TargetLaunchEvent&) const { resolver.LaunchTarget(); }

        void operator()(const ReboundEvent&) const { resolver.ApplyRebound(); }

        void operator()(const CameraShakeEvent& shake) const
        {
            if (resolver.m_hitReaction != nullptr)
            {
                (void)resolver.m_hitReaction->StartShake(resolver.ShakeDescFor(shake, event.length));
            }
        }

        void operator()(const ZoomRollEvent& zoomRoll) const
        {
            if (resolver.m_hitReaction != nullptr)
            {
                (void)resolver.m_hitReaction->StartZoomRoll(resolver.ZoomRollDescFor(zoomRoll, event.length));
            }
        }

        void operator()(const PadVibrationEvent& pad) const
        {
            if (resolver.m_hitReaction != nullptr)
            {
                resolver.m_hitReaction->StartPadVibration(
                    NS::Obj::HitPadVibration{.left = pad.left, .right = pad.right, .frames = event.length});
            }
        }

        void operator()(const FlashEvent& flash) const
        {
            if (resolver.m_hitReaction != nullptr)
            {
                resolver.m_hitReaction->StartFlash(event.length, flash.alpha);
            }
        }

        // 絵は ImpactEffects が自分の更新で出す。ここは頼みを置くだけ
        void operator()(const HitEffectEvent&) const
        {
            if (resolver.m_player != nullptr)
            {
                resolver.m_player->ImpactVisuals().RequestHitEffect();
            }
        }

        void operator()(const FlightEffectEvent&) const
        {
            if (resolver.m_player != nullptr)
            {
                resolver.m_player->ImpactVisuals().RequestFlightEffect();
            }
        }
    };

    void ImpactResolver::StartTimeline(std::vector<HitEvent> events, int breakStopSteps)
    {
        m_events = std::move(events);
        m_breakStopSteps = breakStopSteps;
        m_clock = 0;
        m_clockEnd = 0;
        m_holdArmed = false;
        m_holdReleased = false;
        m_hasReboundEvent = false;
        m_stopStarted = false;
        for (const HitEvent& event : m_events)
        {
            // 長さ 0 の事象も始まりのフレームには起きるので、そのフレームまで時計を回す
            m_clockEnd = std::max(m_clockEnd, event.start + std::max(event.length, 1));
            if (std::holds_alternative<HitStopEvent>(event.value) && StopLengthOf(event, breakStopSteps) > 0)
            {
                m_holdArmed = true;
            }
            if (std::holds_alternative<ReboundEvent>(event.value))
            {
                m_hasReboundEvent = true;
            }
        }
        m_clockRunning = true;
        AdvanceTimeline();
    }

    void ImpactResolver::AbortTimeline() noexcept
    {
        m_events.clear();
        m_clock = 0;
        m_clockEnd = 0;
        m_clockRunning = false;
        m_holdArmed = false;
        m_holdReleased = false;
        m_hasReboundEvent = false;
        m_stopStarted = false;
        m_stopEnd = 0;
        m_breakStopSteps = -1;
        m_shapeActive = false;
    }

    void ImpactResolver::AdvanceTimeline()
    {
        for (const HitEvent& event : m_events)
        {
            if (event.start == m_clock)
            {
                std::visit(EventRunner{.resolver = *this, .event = event}, event.value);
            }
        }
        if (m_stopStarted && m_clock == m_stopEnd + 1)
        {
            m_releasedThisStep = true;
            // 反動の事象の無いタイムラインは、止めの終わりで自機を動かし直す
            if (!m_hasReboundEvent)
            {
                m_holdReleased = true;
            }
        }
        if (m_clock >= m_clockEnd)
        {
            m_clockRunning = false;
            // 反動の事象が止めより前に置かれていても、時計が止まったら自機を動かし直す
            m_holdReleased = true;
        }
    }

    bool ImpactResolver::IsHitStopping() const noexcept
    {
        return m_stopStarted && !m_holdReleased && m_clock <= m_stopEnd;
    }

    bool ImpactResolver::IsAwaitingRebound() const noexcept
    {
        return m_stopStarted && !m_holdReleased && m_clock > m_stopEnd;
    }

    bool ImpactResolver::IsShapeAnimating() const noexcept
    {
        const int frame = m_clock - m_shapeStart;
        return m_shapeActive && frame >= 0 && frame < m_shapeLength;
    }

    NS::Obj::CameraShakeDesc ImpactResolver::ShakeDescFor(const CameraShakeEvent& shake, int length) const noexcept
    {
        NS::Obj::CameraShakeDesc desc;
        desc.longestFlipFrames = shake.longestFlipFrames;
        desc.firstSideDirection = m_pendingReboundArc.direction;
        desc.seed = m_pendingShakeSeed;
        const float weight = std::sqrt(shake.sideWeight * shake.sideWeight + shake.upWeight * shake.upWeight);
        // 非数の重みも向きが決まらないので揺らさない
        if (!(weight > 0.0f))
        {
            return desc;
        }
        desc.frames = length;
        // 威力の手応えは振れ幅で出す。事象の強さに威力と、反発と同じ質量の効きを掛ける
        // 横と縦を合わせた長さが最初の振れの大きさになるよう、重みの比で分ける
        const float amplitude = shake.strength * m_pendingPower * m_pendingMassFactor;
        const float perWeight = amplitude / weight;
        desc.sideAmplitude = shake.sideWeight * perWeight;
        desc.upAmplitude = shake.upWeight * perWeight;
        return desc;
    }

    NS::Obj::CameraZoomRollDesc ImpactResolver::ZoomRollDescFor(const ZoomRollEvent& zoomRoll,
                                                                int length) const noexcept
    {
        const int frames = std::max(length, 0);
        NS::Obj::CameraZoomRollDesc desc;
        desc.zoom = zoomRoll.zoom;
        desc.rollDegrees = zoomRoll.rollDegrees;
        desc.rollDirection = m_pendingImpactDir;
        desc.returnFrames = std::clamp(zoomRoll.returnFrames, 0, frames);
        desc.holdFrames = frames - desc.returnFrames;
        return desc;
    }

    void ImpactResolver::RecordReturns(const std::vector<HitEvent>& events)
    {
        m_lastImpact.cameraShake = 0.0f;
        m_lastImpact.flashStart = 0;
        m_lastImpact.zoomStart = 1.0f;
        m_lastImpact.rollStart = 0.0f;
        m_lastImpact.padStart = NS::Platform::GamepadVibration{};
        bool shakeRecorded = false;
        bool flashRecorded = false;
        bool zoomRollRecorded = false;
        bool padRecorded = false;
        for (const HitEvent& event : events)
        {
            const CameraShakeEvent* shake = std::get_if<CameraShakeEvent>(&event.value);
            if (shake != nullptr && !shakeRecorded)
            {
                const NS::Obj::CameraShakeDesc desc = ShakeDescFor(*shake, event.length);
                m_lastImpact.cameraShake = NS::Core::Vector2{desc.sideAmplitude, desc.upAmplitude}.Length();
                shakeRecorded = true;
            }
            const FlashEvent* flash = std::get_if<FlashEvent>(&event.value);
            if (flash != nullptr && !flashRecorded)
            {
                m_lastImpact.flashStart = std::max(event.length, 0);
                flashRecorded = true;
            }
            const ZoomRollEvent* zoomRoll = std::get_if<ZoomRollEvent>(&event.value);
            if (zoomRoll != nullptr && !zoomRollRecorded)
            {
                // 当たりの記録は検知のフレームに読まれるので、傾きの向きもここで今のカメラから決める
                const float rollSign = NS::Obj::CameraSideSignOf(*Owner(), m_pendingImpactDir);
                m_lastImpact.zoomStart = zoomRoll->zoom;
                m_lastImpact.rollStart = zoomRoll->rollDegrees * rollSign;
                zoomRollRecorded = true;
            }
            const PadVibrationEvent* pad = std::get_if<PadVibrationEvent>(&event.value);
            if (pad != nullptr && !padRecorded)
            {
                m_lastImpact.padStart.left = pad->left.Evaluate(0.0f);
                m_lastImpact.padStart.right = pad->right.Evaluate(0.0f);
                padRecorded = true;
            }
        }
    }

    void ImpactResolver::OnEndPlay()
    {
        CancelImpact();
    }

    void ImpactResolver::CancelImpact() noexcept
    {
        // 白・揺れ・振動を始めたのは裁定役なので止めるのも裁定役。自機を止めていない時は前の当たりの薄れを残す
        const bool hadStop = IsHoldingPlayer();
        AbortTimeline();
        m_pendingBreak = false;
        m_hasObservedTarget = false;
        m_stateReady = false;
        m_didRebound = false;
        m_didBreak = false;
        m_freezeBeganThisStep = false;
        m_releasedThisStep = false;
        // 相手へは明けを送らない。相手はやり直しの知らせで自分の位置へ戻り、凍結は相手の数えで明ける
        m_pendingTarget = NS::Obj::ActorRef{};
        if (hadStop && m_hitReaction != nullptr)
        {
            m_hitReaction->Stop();
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

    void ImpactResolver::ApplyRebound()
    {
        m_holdReleased = true;
        if (m_pendingBreak)
        {
            m_body->SetVelocity(m_pendingSelfVelocity);
            return;
        }
        if (!m_player->BeginRebound(m_pendingReboundArc))
        {
            // 欄が曲線にならない値の時だけ通る。書かないと、止める前の最後のフレームの速度のまま動き出す
            m_body->SetVelocity(m_pendingSelfVelocity);
            NS_LOG_WARN(Game,
                        "反動が曲線にならず、自機を弾けなかった: 高さ {} 距離 {}",
                        m_pendingReboundArc.apexHeight,
                        m_pendingReboundArc.distance);
        }
    }

    void ImpactResolver::LaunchTarget()
    {
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
                                        .breaks = m_pendingBreak,
                                        .tier = m_pendingTier,
                                        .power = m_lastPower,
                                        .launchScale = m_pendingLaunchScale};
        (void)SendMsgTackleRelease(*target, release);
    }

    NS::Core::Vector3 ImpactResolver::ShapeFactors() const noexcept
    {
        // 事象の外は補間の残差を残さず、ちょうど 1 を返す
        if (!IsShapeAnimating())
        {
            return NS::Core::Vector3{1.0f, 1.0f, 1.0f};
        }
        const float frame = static_cast<float>(m_clock - m_shapeStart);
        return AlongImpactFactors(CurveFactor(m_shape.along, frame), CurveFactor(m_shape.height, frame));
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
