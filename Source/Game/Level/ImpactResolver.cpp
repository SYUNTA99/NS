#include "Game/Level/ImpactResolver.h"

#include "Game/Level/HitZones.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/ImpactTremor.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "Game/Player.h"
#include "Game/Player/ImpactEffects.h"
#include "Game/Player/LaunchPitch.h"
#include "Game/Player/PlayerParams.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/ActorList.h"
#include "NSlib/Object/IUse/IUseCamera.h"
#include "NSlib/Object/IUse/IUseSceneObj.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/HitScreenDirector.h"
#include "NSlib/Object/Scene/HitSensorDirector.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/SubObjects/Collider.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Physics/Capsule.h"
#include "NSlib/Windows/Clock.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <utility>
#include <variant>
#include <vector>

namespace GL::Level
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
                                              const NS::Vector3& impactDir,
                                              const NS::Vector3& targetPos)
        {
            const float headingDegrees = NS::RadiansToDegrees(std::atan2(impactDir.z, impactDir.x));
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
        [[nodiscard]] LineOffset MeasureLineOffset(const NS::Vector3& position,
                                                   const NS::AABB& bounds,
                                                   const NS::Vector3& direction,
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

        // 半径 radius の玉が origin から direction へ distance 進む間に通る所。玉を線分に沿って掃いた形はカプセル
        // 事前条件: direction が正規化済み、distance が 0 以上で有限
        [[nodiscard]] NS::Phys::Capsule SweptBall(const NS::Vector3& origin,
                                                  const NS::Vector3& direction,
                                                  float distance,
                                                  float radius) noexcept
        {
            const float half = distance * 0.5f;
            return NS::Phys::Capsule{
                .center = origin + direction * half, .axis = direction, .halfHeight = half, .radius = radius};
        }

        // 接触距離を指定した誤差まで詰め、触れる側の端を返す
        // 掃く長さを伸ばすほど触れる形は増えるだけなので、触れない長さと触れる長さの間を半分ずつ詰める
        // 事前条件: SweptBall(origin, direction, distance, radius) が target に触れている
        [[nodiscard]] float FirstTouchDistance(const NS::Obj::SensorVolume& target,
                                               const NS::Vector3& origin,
                                               const NS::Vector3& direction,
                                               float distance,
                                               float radius,
                                               float tolerance) noexcept
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
            while (touched - missed > tolerance)
            {
                const float middle = (missed + touched) * 0.5f;
                if (middle == missed || middle == touched)
                {
                    break;
                }
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
        [[nodiscard]] NS::Vector3 PositionAfterStep(const NS::Vector3& position, const NS::Vector3& velocity) noexcept
        {
            const float dt = NS::OS::FrameTimer::FixedDelta();
            return NS::Vector3{
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

        // 曲線の値を scale 倍にする。点の値と傾きを同じ倍率で掛けるので、補間の形は変わらない
        [[nodiscard]] NS::Obj::Curve ScaledCurve(const NS::Obj::Curve& curve, float scale) noexcept
        {
            NS::Obj::Curve scaled = curve;
            for (std::uint32_t i = 0; i < scaled.count; ++i)
            {
                scaled.keys[i].y *= scale;
                scaled.keys[i].inTangent *= scale;
                scaled.keys[i].outTangent *= scale;
            }
            return scaled;
        }

        // timeline のうち、面の上の位置 face の当たりで起きる事象。並びの順は保つ
        // 向きの付いた振動は、位置の角度で隣り合う 2 つの向きを混ぜるので、重みが 0 でない行を重みを掛けて残す
        // 他の種類の向きの付いた行は、HitDirectionOf の向きの行だけ残す。face が無ければ向きの付いた行は起こさない
        // outRows に、選んだ事象のそれぞれのファイルの並びでの番号を同じ並びで入れる
        [[nodiscard]] std::vector<HitEvent> EventsFor(const HitTimeline& timeline,
                                                      const std::optional<NS::Vector2>& face,
                                                      std::vector<std::size_t>& outRows)
        {
            std::vector<HitEvent> events;
            events.reserve(timeline.events.size());
            outRows.clear();
            for (std::size_t row = 0; row < timeline.events.size(); ++row)
            {
                const HitEvent& event = timeline.events[row];
                if (event.direction == HitDirection::Any)
                {
                    events.push_back(event);
                    outRows.push_back(row);
                    continue;
                }
                if (!face.has_value())
                {
                    continue;
                }
                const PadVibrationEvent* pad = std::get_if<PadVibrationEvent>(&event.value);
                if (pad == nullptr)
                {
                    if (event.direction == HitDirectionOf(face->x, face->y))
                    {
                        events.push_back(event);
                        outRows.push_back(row);
                    }
                    continue;
                }
                const float weight = HitDirectionWeight(event.direction, face->x, face->y);
                if (weight <= 0.0f)
                {
                    continue;
                }
                HitEvent weighted = event;
                weighted.value =
                    PadVibrationEvent{.left = ScaledCurve(pad->left, weight), .right = ScaledCurve(pad->right, weight)};
                events.push_back(weighted);
                outRows.push_back(row);
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

    const GL::Player::PlayerParams& ImpactResolver::Tuning() const noexcept
    {
        // 呼ぶのは OnStart が持ち主を引き当てた後だけ
        return m_player->Params();
    }

    void ImpactResolver::OnStart()
    {
        if (::Player* ownerPlayer = NS::Obj::Cast<::Player>(Owner()))
        {
            m_player = ownerPlayer;
            m_body = &ownerPlayer->Body();
            m_hitReaction = ownerPlayer->HitReactionSubObj();
        }
    }

    int ImpactResolver::CenterHitFlashStepsRemaining() const noexcept
    {
        if (Owner() == nullptr)
        {
            return 0;
        }
        const NS::Obj::HitScreenDirector* screen = NS::Obj::FindSceneObj<NS::Obj::HitScreenDirector>(*Owner());
        if (screen == nullptr)
        {
            return 0;
        }
        return screen->FlashFramesRemaining();
    }

    NS::Obj::HitSensor* ImpactResolver::FindOverlapped(const NS::Vector3& predictedVelocity) const
    {
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return nullptr;
        }

        const NS::Vector3 position = Owner()->Root().Position();

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
            const NS::AABB bounds = sensor->WorldVolume().Bounds();
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

    bool ImpactResolver::FindSlamLineTarget(const NS::Vector3& direction,
                                            float maxDistance,
                                            SlamLineTarget& outTarget) const
    {
        NS::Vector3 lineDir{};
        if (Owner() == nullptr || m_body == nullptr || !NS::TryNormalizeHorizontal(direction, lineDir))
        {
            return false;
        }
        // 非数と無限の向きは正規化を通り抜ける
        if (!NS::IsFinite(lineDir))
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

        const NS::Vector3 position = Owner()->Root().Position();
        const float playerRadius = m_player->Collider().CapsuleRadius();
        // 突進は丸まった玉で進む。玉の決まりは Player::SlamBallAt が持つ
        const NS::Vector3 ballCenter = m_player->SlamBallAt(position).center;
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
            const NS::AABB bounds = volume.Bounds();

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
            // 最初に触れる相手は、中心の近さでなく玉が触れるまでに進む距離で決める。突進はそこで止まって当たる
            const float contact = FirstTouchDistance(
                volume, ballCenter, lineDir, maxDistance, playerRadius, std::max(Tuning().m_contactTolerance, 0.0f));
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
            const GL::Player::PlayerParams& params = Tuning();
            float aimHeight = ballCenter.y;
            (void)HitFaceAimHeight(answer.face, answer.body, lineDir, playerRadius, aimHeight);
            // 触れる所は、着きたい高さで線を進めた玉が触れる所。今の高さで測ると、高さの違う相手の上の縁をかすめる
            // 所まで寄ってしまい、弧の着く所と実際に触れる所がずれる。その高さで触れない時は今の高さの値のまま
            const NS::Vector3 aimedCenter{ballCenter.x, aimHeight, ballCenter.z};
            float aimedContact = first.contact;
            if (NS::Obj::VolumesOverlap(
                    NS::Obj::SensorVolume::Capsule(SweptBall(aimedCenter, lineDir, maxDistance, playerRadius)),
                    firstSensor->WorldVolume()))
            {
                aimedContact = FirstTouchDistance(firstSensor->WorldVolume(),
                                                  aimedCenter,
                                                  lineDir,
                                                  maxDistance,
                                                  playerRadius,
                                                  std::max(params.m_contactTolerance, 0.0f));
            }
            const float dt = NS::OS::FrameTimer::FixedDelta();
            const GL::Player::LaunchPitchResult pitch = GL::Player::LaunchPitch(
                GL::Player::LaunchPitchDesc{.ballHeight = ballCenter.y,
                                            .targetHeight = aimHeight,
                                            .contactDistance = aimedContact,
                                            .horizontalSpeed = params.m_bodySlamSpeed,
                                            .gravity = params.Gravity(),
                                            .maxAngleDegrees = params.m_launchPitchLimitDegrees,
                                            .grounded = m_body->IsGrounded(),
                                            .dt = dt,
                                            .heightTolerance = params.m_launchHeightTolerance,
                                            .angleGuardDegrees = params.m_launchAngleGuardDegrees,
                                            .maxFrames = params.m_launchMaxFrames});
            first.launchVerticalSpeed = pitch.verticalSpeed;
            if (pitch.reachable)
            {
                first.launchContact = aimedContact;
            }
            // 段と横ずれは裁定と同じく相手の答えの面で決める。玉の高さは放つ縦の速さの道筋が触れる所で居る高さ
            const GL::Player::LaunchPath path{.horizontalSpeed = params.m_bodySlamSpeed,
                                              .verticalSpeed = pitch.verticalSpeed,
                                              .gravity = params.Gravity(),
                                              .dt = dt,
                                              .grounded = m_body->IsGrounded(),
                                              .maxFrames = params.m_launchMaxFrames};
            const NS::Vector3 arrival{
                ballCenter.x, ballCenter.y + GL::Player::LaunchHeightAt(path, first.launchContact), ballCenter.z};
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
        const NS::Vector3 predictedVelocity = m_player->BodySlamVelocity();
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
        m_framesToPredictedContact = -1;
        m_rowsStartedThisStep.clear();
        // 白の光と振動の進みは HitReaction が持つ。止まっている間も薄れる
        if (m_body == nullptr)
        {
            return;
        }

        if (m_clockRunning)
        {
            // 触れる前の時計は検知を待つ間 -1 で留まる。0 は検知のフレームが置く
            if (!m_beforeContact || m_clock < -1)
            {
                ++m_clock;
                AdvanceTimeline();
            }
        }
        AdvanceBodyShake();
        AdvanceTremor();
        AdvanceGroundWave();
        AdvanceDistortionRing();
        // 止めた自機を動かし直すまでは新しい衝突を見ない。止まった自機は重なったままなので、見ると毎フレーム検知し直す
        if (IsHoldingPlayer())
        {
            return;
        }

        // 押していない接触は物理の停止だけで済ませるため、体当たり中でないフレームは裁定しない
        if (!m_player->IsBodySlamming())
        {
            // 当たらずに突進が終わったので、触れる前の予測は外れた
            DropBeforeContact();
            return;
        }
        if (ResolveObservedHit(hadObservation))
        {
            return;
        }
        UpdateBeforeContact();
    }

    bool ImpactResolver::ResolveObservedHit(bool hadObservation)
    {
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (!hadObservation || scene == nullptr)
        {
            return false;
        }
        const NS::Obj::Actor* target = scene->Objects().FindObject(m_observedTarget);
        if (target == nullptr || !target->IsActiveInHierarchy())
        {
            return false;
        }
        const TackleTargetAnswer& answer = m_observedAnswer;
        const NS::AABB bounds = answer.bounds;

        const NS::Vector3 position = Owner()->Root().Position();
        // 箱へ押し付けられたフレームは実速度が 0 に潰されるため、突進の狙いの速度で向きと貫通後の速度を決める
        const NS::Vector3 velocity = m_observedVelocity;

        // 弾かれる向きは箱と自機の並びで決まる。水平だけを見て、上向きは反動の高さから出す
        NS::Vector3 away{};
        // 箱の中心へ重なると向きが決まらない。進んできた向きの逆へ弾く
        if (!NS::TryNormalizeHorizontal(position - NS::Vector3{bounds.Center}, away) &&
            !NS::TryNormalizeHorizontal(-velocity, away))
        {
            return false;
        }
        const float awayX = away.x;
        const float awayZ = away.z;

        // 箱へ向かっているフレームだけ弾く。離れていく間も弾くと、重なりが解けるまで毎フレーム掛かり直す
        if (velocity.x * awayX + velocity.z * awayZ >= 0.0f)
        {
            return false;
        }

        const float charge01 = m_player->BodySlamCharge01();

        const float mass = answer.mass;

        // 段と威力の当たり位置の係数は、相手の面で当てはまった同じ決まりから取る
        // 玉の中心は重なりを見た所と同じく、この固定ステップで進んだ先。今の位置で見ると、縦に動く突進は 1 ステップ
        // ぶん違う高さで段が決まる。玉は狙う相手の探し方と同じ Player::SlamBallAt から引く
        const NS::Vector3 stepped = PositionAfterStep(position, velocity);
        const NS::Vector3 ballCenter = m_player->SlamBallAt(stepped).center;
        const HitFaceJudgement judgement =
            JudgeHitFaceOrWide(answer.face, answer.body, ballCenter, velocity, m_player->Collider().CapsuleRadius());
        const float offset01 = judgement.offset01;
        const float chargeFactor = Tuning().ChargeFactorFor(charge01, m_player->BodySlamOvercharge01());
        const float positionFactor = judgement.powerScale;
        const HitTier tier = judgement.tier;
        // 読むのは記録とログだけ
        const bool centerHit = tier == HitTier::Center;
        // 最終威力 = チャージ倍率 × 当たり位置係数。破壊の判定だけでなく反発・発射・揺れも威力で作る
        const float power = chargeFactor * positionFactor;
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
        // 相手は突進の向きへ飛ばす。中心の並びで飛ばすと、横ずれのある当たりが狙いと別の所へ飛ぶ
        // 突進の水平の速さがほぼ 0 で向きが決まらない時だけ、中心の並びの向きへ飛ばす
        NS::Vector3 launchDir{-awayX, 0.0f, -awayZ};
        NS::Vector3 slamDir{};
        if (NS::TryNormalizeHorizontal(velocity, slamDir))
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
        impactInput.launch = answer.launch;
        impactInput.awayDirection = NS::Vector3{awayX, 0.0f, awayZ};
        impactInput.launchDirection = launchDir;
        impactInput.slamVelocity = velocity;
        impactInput.faceU = judgement.u;
        impactInput.faceV = judgement.v;
        impactInput.bodyShape = judgement.bodyShape;
        const ImpactOutcome outcome = ComputeImpactOutcome(impactInput, MakeImpactTuning(Tuning()));

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

        const HitDirection direction = HitDirectionOf(judgement.u, judgement.v);
        const HitTimeline* timeline = HitTimelineLibrary::Get().FindForTier(tier);
        // 同じ相手と同じ段の予測で触れる前の時計を始めていれば、止めずに 0 から続ける
        // それ以外で前の当たりの事象が走っていれば、この当たりの事象を起こす前に止める。始めた返りも止め、
        // この当たりのタイムラインに置いた返りだけが出る
        const bool continuesBeforeContact = m_beforeContact && timeline != nullptr &&
                                            m_beforeContactTarget == m_pendingTarget && m_beforeContactTier == tier;
        if (!continuesBeforeContact)
        {
            if (m_clockRunning && m_hitReaction != nullptr)
            {
                m_hitReaction->Stop();
            }
            AbortTimeline();
        }
        m_beforeContact = false;
        std::vector<HitEvent> events;
        std::vector<std::size_t> rows;
        if (timeline != nullptr)
        {
            events = EventsFor(*timeline, NS::Vector2{judgement.u, judgement.v}, rows);
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
        m_pendingShakeSeed = ShakeSeed(m_pendingTarget.id, offset01, m_pendingImpactDir, answer.position);
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
        m_lastImpact.faceU = judgement.u;
        m_lastImpact.faceV = judgement.v;
        m_lastImpact.hitStopSteps = stopSteps;
        m_lastImpact.localStop = stopSteps > 0;
        m_lastImpact.overcharge01 = m_player->BodySlamOvercharge01();
        const bool overcharged = m_lastImpact.overcharge01 > 0.0f;
        m_lastImpact.gradualRelease =
            std::any_of(events.begin(), events.end(), [overcharged](const HitEvent& event) noexcept {
                const GradualReleaseEvent* release = std::get_if<GradualReleaseEvent>(&event.value);
                return release != nullptr && (overcharged || !release->overchargedOnly);
            });
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
        m_lastImpact.targetPos = answer.position;
        m_lastImpact.targetBottom = bounds.Center.y - bounds.Extents.y;
        m_lastImpact.targetMass = mass;
        m_lastImpact.targetPlaced = answer.placed;
        m_lastImpact.launchScale = launchScale;
        m_lastImpact.reboundScale = reboundScale;

        // 遊びの結果は配分で決まっているので、返りを置けない当たりも反動と飛ばしだけは出す
        if (timeline == nullptr)
        {
            ApplyRebound();
            LaunchTarget();
            return true;
        }
        StartTimeline(std::move(events), std::move(rows), breakStopSteps, !continuesBeforeContact);
        return true;
    }

    // 事象の種類ごとの受け持ち。種類を足して受け持ちを書き忘れると、std::visit が呼べずにコンパイルが止まる
    struct ImpactResolver::EventRunner
    {
        ImpactResolver& resolver;
        const HitEvent& event;

        void operator()(const HitStopEvent&) const
        {
            const int length = StopLengthOf(event, resolver.m_breakStopSteps);
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
            NS::Obj::Actor* target = scene->Objects().FindObject(resolver.m_pendingTarget);
            if (target == nullptr)
            {
                return;
            }
            const TackleFreezeDesc desc{.impactDir = resolver.m_pendingImpactDir,
                                        .pushInDistance = freeze.pushInDistance,
                                        .squashThickness = freeze.squashThickness,
                                        .squashHeight = freeze.squashHeight,
                                        .squash = !resolver.m_pendingBreak,
                                        .stopSteps = event.length};
            (void)SendMsgTackleFreeze(*target, desc);
        }

        void operator()(const TargetLaunchEvent&) const { resolver.LaunchTarget(); }

        // 横揺れと震えは行の長さを使わず、この当たりで効く止めと同じフレーム数だけ出す。止めの長さを変えても
        // (貫通の止めを含む) 揺れだけが残る・先に切れる事が無い
        void operator()(const BodyShakeEvent& shake) const
        {
            resolver.StartBodyShake(shake, StopStepsOf(resolver.m_events, resolver.m_breakStopSteps));
        }

        void operator()(const ImpactTremorEvent& tremor) const
        {
            resolver.StartTremor(tremor, StopStepsOf(resolver.m_events, resolver.m_breakStopSteps));
        }

        // 震えの線も横揺れと同じく、止めと同じフレーム数だけ出す
        void operator()(const ShakeLinesEvent& lines) const
        {
            resolver.StartShakeLines(lines, StopStepsOf(resolver.m_events, resolver.m_breakStopSteps));
        }

        void operator()(const GroundWaveEvent& wave) const { resolver.StartGroundWave(wave, event.length); }

        void operator()(const DistortionRingEvent& ring) const { resolver.StartDistortionRing(ring, event.length); }

        void operator()(const ReboundEvent&) const { resolver.ApplyRebound(); }

        void operator()(const CameraShakeEvent& shake) const
        {
            if (resolver.m_hitReaction != nullptr)
            {
                (void)resolver.m_hitReaction->StartShake(resolver.ShakeDescFor(shake, event.length));
            }
        }

        void operator()(const CameraLurchEvent& lurch) const
        {
            // 突進の向きへ世界の向きのまま押す。外した事を、体が前へ持って行かれる事で見せる
            resolver.StartCameraNudge(resolver.m_pendingImpactDir, lurch.distance, event.length, false);
        }

        void operator()(const CameraReboundSwayEvent& sway) const
        {
            // カメラから見た反動の向きで、逆へ大きく振れて縮み、最後に反動の側へ速く送る
            resolver.StartCameraNudge(resolver.m_pendingReboundArc.direction, sway.distance, event.length, true);
        }

        void operator()(const CameraSinkEvent& sink) const
        {
            if (resolver.m_hitReaction != nullptr)
            {
                (void)resolver.m_hitReaction->StartSink(resolver.SinkDescFor(sink, event));
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
            if (resolver.m_hitReaction == nullptr)
            {
                return;
            }
            const NS::Obj::HitPadVibration vibration{.left = pad.left, .right = pad.right, .frames = event.length};
            // 同じフレームに始まる振動 (向きごとに重みを掛けた行) は重ねて鳴らす
            if (resolver.m_padStartClock == resolver.m_clock)
            {
                resolver.m_hitReaction->BlendPadVibration(vibration);
                return;
            }
            resolver.m_hitReaction->StartPadVibration(vibration);
            resolver.m_padStartClock = resolver.m_clock;
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

        void operator()(const CameraTraumaEvent& trauma) const
        {
            if (resolver.m_hitReaction != nullptr)
            {
                (void)resolver.m_hitReaction->AddTrauma(resolver.TraumaDescFor(trauma));
            }
        }

        void operator()(const GradualReleaseEvent& release) const
        {
            NS::Obj::Scene* scene = resolver.Owner()->OwningScene();
            if (scene == nullptr)
            {
                return;
            }
            // 紫で上がった威力を当たった結果で見せる遅さなので、赤で放した当たりには出さない
            if (release.overchargedOnly && !(resolver.m_lastImpact.overcharge01 > 0.0f))
            {
                return;
            }
            // 戻りは世界の速さの持ち主が進める。ここは始めるだけ
            scene->StartWorldSpeedRamp(release.startSpeed, release.returnSeconds, release.shape);
            resolver.m_startedGradualRelease = true;
        }

        void operator()(const OthersStopEvent&) const
        {
            NS::Obj::Scene* scene = resolver.Owner()->OwningScene();
            if (scene == nullptr)
            {
                return;
            }
            // 止めの数えは世界の持ち主が進める。ここは始めるだけ
            scene->HoldOthers(event.length);
            resolver.m_heldOthers = true;
        }

        void operator()(const FlightEffectEvent&) const
        {
            if (resolver.m_player != nullptr)
            {
                resolver.m_player->ImpactVisuals().RequestFlightEffect();
            }
        }
    };

    void ImpactResolver::StartTimeline(std::vector<HitEvent> events,
                                       std::vector<std::size_t> rows,
                                       int breakStopSteps,
                                       bool startEarlyEvents)
    {
        m_events = std::move(events);
        m_eventRows = std::move(rows);
        m_breakStopSteps = breakStopSteps;
        // 前の当たりの横揺れ・震え・床の波は、新しい時計で数えると長さの終わりが来ない
        StopBodyShake();
        StopTremor();
        StopGroundWave();
        StopDistortionRing();
        m_clock = 0;
        m_padStartClock.reset();
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
        // 触れる前の時計を回せなかった当たり (接した所から放した時・予測より先に触れた時) でも、触れた後まで続く
        // 形を落とすと潰れごと消える。触れる前の分は飛ばし、置いたフレームを始まりにして途中から起こす
        if (startEarlyEvents)
        {
            for (std::size_t i = 0; i < m_events.size(); ++i)
            {
                const HitEvent& event = m_events[i];
                if (event.start < 0 && event.start + std::max(event.length, 1) > 0 &&
                    CanStartBeforeContact(event.value))
                {
                    m_clock = event.start;
                    m_rowsStartedThisStep.push_back(m_eventRows[i]);
                    std::visit(EventRunner{.resolver = *this, .event = event}, event.value);
                }
            }
            m_clock = 0;
        }
        AdvanceTimeline();
    }

    void ImpactResolver::UpdateBeforeContact()
    {
        // 触れる前の事象を置いた段が無ければ、線を掃かない
        int earliest = 0;
        for (const HitTier tier : HitTiers())
        {
            if (const HitTimeline* timeline = HitTimelineLibrary::Get().FindForTier(tier))
            {
                for (const HitEvent& event : timeline->events)
                {
                    earliest = std::min(earliest, event.start);
                }
            }
        }
        if (earliest >= 0)
        {
            DropBeforeContact();
            return;
        }

        const NS::Vector3 velocity = m_player->BodySlamVelocity();
        NS::Vector3 direction{};
        const float stepLength =
            std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z) * NS::OS::FrameTimer::FixedDelta();
        SlamLineTarget predicted{};
        if (!NS::TryNormalizeHorizontal(velocity, direction) || !(stepLength > 0.0f) ||
            !FindSlamLineTarget(direction, m_player->BodySlamDistance(), predicted))
        {
            DropBeforeContact();
            return;
        }
        // 観測の段は、今の位置から 1 歩進んだ玉の重なりで当たりを見る。触れるまでの距離を k 歩目に越えるなら、
        // 検知はこのフレームから k - 1 フレーム後。このフレームで検知しなかったので 1 以上
        m_framesToPredictedContact = std::max(static_cast<int>(std::ceil(predicted.contact / stepLength)) - 1, 1);
        if (m_beforeContact)
        {
            if (predicted.target == m_beforeContactTarget && predicted.tier == m_beforeContactTier)
            {
                return;
            }
            DropBeforeContact();
        }

        const HitTimeline* timeline = HitTimelineLibrary::Get().FindForTier(predicted.tier);
        if (timeline == nullptr)
        {
            return;
        }
        // 触れる前は外れの向きが決まらないので、向きの付いた行は起こさない
        std::vector<std::size_t> rows;
        std::vector<HitEvent> events = EventsFor(*timeline, std::nullopt, rows);
        int first = 0;
        for (const HitEvent& event : events)
        {
            first = std::min(first, event.start);
        }
        const int untilDetect = m_framesToPredictedContact;
        if (first >= 0 || untilDetect > -first)
        {
            return;
        }

        // 前の当たりの返りが残っていれば、検知の時と同じく止めてから始める
        if (m_clockRunning && m_hitReaction != nullptr)
        {
            m_hitReaction->Stop();
        }
        AbortTimeline();
        m_events = std::move(events);
        m_eventRows = std::move(rows);
        m_clock = -untilDetect;
        for (const HitEvent& event : m_events)
        {
            m_clockEnd = std::max(m_clockEnd, event.start + std::max(event.length, 1));
        }
        m_clockRunning = true;
        m_beforeContact = true;
        m_beforeContactTarget = predicted.target;
        m_beforeContactTier = predicted.tier;
        // 形は相手の飛ぶ向き (突進の向き) で混ぜる。検知のフレームに当たりの向きで置き直す
        m_pendingImpactDir = direction;
        m_pendingBreak = false;
        // 予測が遅れて今のフレームより前に始まるはずだった事象は、置いたフレームを始まりにして今起こす
        const int now = m_clock;
        for (std::size_t i = 0; i < m_events.size(); ++i)
        {
            const HitEvent& event = m_events[i];
            if (event.start <= now && event.start < 0 && CanStartBeforeContact(event.value))
            {
                m_clock = event.start;
                m_rowsStartedThisStep.push_back(m_eventRows[i]);
                std::visit(EventRunner{.resolver = *this, .event = event}, event.value);
            }
        }
        m_clock = now;
    }

    void ImpactResolver::DropBeforeContact() noexcept
    {
        if (m_beforeContact)
        {
            AbortTimeline();
        }
    }

    void ImpactResolver::AbortTimeline() noexcept
    {
        // 段階的な明けは時計より長く続く。打ち切る当たりが遅くした世界を普段の速さへ戻す
        if (m_startedGradualRelease)
        {
            m_startedGradualRelease = false;
            if (Owner() != nullptr && Owner()->OwningScene() != nullptr)
            {
                Owner()->OwningScene()->SetWorldSpeed(1.0f);
            }
        }
        // 外れた予測の止めを残すと、当たらないのに世界が止まったままになる
        if (m_heldOthers)
        {
            m_heldOthers = false;
            if (Owner() != nullptr && Owner()->OwningScene() != nullptr)
            {
                Owner()->OwningScene()->HoldOthers(0);
            }
        }
        m_beforeContact = false;
        StopBodyShake();
        StopTremor();
        StopGroundWave();
        StopDistortionRing();
        m_events.clear();
        m_eventRows.clear();
        m_padStartClock.reset();
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
        for (std::size_t i = 0; i < m_events.size(); ++i)
        {
            const HitEvent& event = m_events[i];
            // 触れる前に置けない種類は、ファイルの読み込みで弾いている。手で組んだ並びでもマイナスでは起こさない
            if (event.start == m_clock && (m_clock >= 0 || CanStartBeforeContact(event.value)))
            {
                m_rowsStartedThisStep.push_back(m_eventRows[i]);
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

    NS::Obj::CameraSinkDesc ImpactResolver::SinkDescFor(const CameraSinkEvent& sink,
                                                        const HitEvent& event) const noexcept
    {
        // 頭打ちの曲線。威力の基準が 0 以下か非数なら、どの威力でも頭打ちの深さにする
        float scale = 1.0f;
        if (sink.powerBase > 0.0f && std::isfinite(m_pendingPower))
        {
            scale = 1.0f - std::exp(-m_pendingPower / sink.powerBase);
        }
        // 跳ね返りは同じタイムラインの反動の事象の始まりから。無ければ跳ね返らずに底のまま終わる
        int bounceStart = event.length;
        for (const HitEvent& other : m_events)
        {
            if (std::holds_alternative<ReboundEvent>(other.value) && other.start >= event.start)
            {
                bounceStart = std::min(bounceStart, other.start - event.start);
            }
        }
        return NS::Obj::CameraSinkDesc{.bottomPixels = sink.maxPixels * scale,
                                       .sinkFrames = sink.sinkFrames,
                                       .tremblePixels = sink.tremblePixels * scale,
                                       .trembleFrames = sink.trembleFrames,
                                       .tremblePeriodFrames = sink.tremblePeriodFrames,
                                       .bounceStartFrame = bounceStart,
                                       .overshootRatio = sink.overshootRatio,
                                       .bouncePeriodFrames = sink.bouncePeriodFrames,
                                       .frames = event.length};
    }

    void ImpactResolver::StartCameraNudge(const NS::Vector3& direction,
                                          const NS::Obj::Curve& distance,
                                          int length,
                                          bool onScreen)
    {
        NS::Vector3 horizontal{};
        if (m_hitReaction == nullptr || !NS::TryNormalizeHorizontal(direction, horizontal))
        {
            return;
        }
        (void)m_hitReaction->StartNudge(NS::Obj::CameraNudgeDesc{
            .direction = horizontal, .distance = distance, .frames = length, .onScreen = onScreen});
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

    NS::Obj::CameraTraumaDesc ImpactResolver::TraumaDescFor(const CameraTraumaEvent& trauma) const noexcept
    {
        NS::Obj::CameraTraumaDesc desc;
        // 外れは自分でしくじった手応え。相手の重さでなく、出した威力で揺らす
        desc.trauma = std::max(trauma.trauma * m_pendingPower, 0.0f);
        desc.shape.yawDegrees = trauma.yawDegrees;
        desc.shape.pitchDegrees = trauma.pitchDegrees;
        desc.shape.rollDegrees = trauma.rollDegrees;
        desc.shape.frequency = trauma.frequency;
        desc.shape.decayPerSecond = trauma.decayPerSecond;
        desc.shape.exponent = trauma.exponent;
        desc.seed = m_pendingShakeSeed;
        desc.kick.degrees = trauma.kickDegrees;
        desc.kick.peakFrames = trauma.kickPeakFrames;
        desc.kick.direction = NS::Vector2{m_lastImpact.faceU, m_lastImpact.faceV};
        if (desc.kick.direction.LengthSquared() <= NS::k_Epsilon * NS::k_Epsilon)
        {
            desc.kick.direction = NS::Vector2{NS::Obj::CameraSideSignOf(*Owner(), m_pendingReboundArc.direction), 0.0f};
        }
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
        m_lastImpact.cameraTrauma = 0.0f;
        m_lastImpact.flashStart = 0;
        m_lastImpact.zoomStart = 1.0f;
        m_lastImpact.rollStart = 0.0f;
        m_lastImpact.padStart = NS::OS::GamepadVibration{};
        bool shakeRecorded = false;
        bool traumaRecorded = false;
        bool flashRecorded = false;
        bool zoomRollRecorded = false;
        bool padRecorded = false;
        int padStartFrame = 0;
        for (const HitEvent& event : events)
        {
            const CameraShakeEvent* shake = std::get_if<CameraShakeEvent>(&event.value);
            if (shake != nullptr && !shakeRecorded)
            {
                const NS::Obj::CameraShakeDesc desc = ShakeDescFor(*shake, event.length);
                m_lastImpact.cameraShake = NS::Vector2{desc.sideAmplitude, desc.upAmplitude}.Length();
                shakeRecorded = true;
            }
            const CameraTraumaEvent* trauma = std::get_if<CameraTraumaEvent>(&event.value);
            if (trauma != nullptr && !traumaRecorded)
            {
                m_lastImpact.cameraTrauma = TraumaDescFor(*trauma).trauma;
                traumaRecorded = true;
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
            // 最初の振動と同じフレームに始まる振動は重ねて鳴らすので、始めの値も足す
            const PadVibrationEvent* pad = std::get_if<PadVibrationEvent>(&event.value);
            if (pad != nullptr && (!padRecorded || event.start == padStartFrame))
            {
                m_lastImpact.padStart.left += pad->left.Evaluate(0.0f);
                m_lastImpact.padStart.right += pad->right.Evaluate(0.0f);
                padStartFrame = event.start;
                padRecorded = true;
            }
        }
    }

    void ImpactResolver::OnEndPlay()
    {
        CancelImpact();
    }

    void ImpactResolver::StartBodyShake(const BodyShakeEvent& shake, int length)
    {
        StopBodyShake();
        if (Owner() == nullptr || length <= 0 || !std::isfinite(shake.amplitudePixels) ||
            !std::isfinite(shake.selfAmplitudePixels))
        {
            return;
        }
        const std::optional<NS::Obj::CameraPose> pose = NS::Obj::CameraViewPose(*Owner());
        if (!pose.has_value())
        {
            return;
        }
        // 画面の横を床に沿わせた向き。後ろから見る NS では、地上でも空中でも視線に直角でよく見える
        const NS::Vector3 forward = NS::Obj::CameraForwardHorizontal(*Owner());
        const NS::Vector3 axis{forward.z, 0.0f, -forward.x};
        // 1 フレーム目の向き。外れは自機が外した側 (面の上の位置 u の側) へ逃げ、カメラの最初のひと揺れ・火花・
        // 逸れ方と揃える。真ん中は画面の右から
        float firstSign = 1.0f;
        NS::Vector3 slam{};
        if (m_lastImpact.tier == HitTier::Wide && m_lastImpact.faceU != 0.0f &&
            NS::TryNormalizeHorizontal(m_pendingImpactDir, slam))
        {
            const NS::Vector3 faceRight{slam.z, 0.0f, -slam.x};
            if (NS::Dot(faceRight * m_lastImpact.faceU, axis) < 0.0f)
            {
                firstSign = -1.0f;
            }
        }
        // 種は何回目の当たりか。毎回少し違い、同じ入力の再生では同じ
        const std::uint32_t seed = m_lastImpact.sequence;
        m_bodyShake =
            BodyShakeRun{.desc = TackleShakeDesc{.axis = axis,
                                                 .amplitude = ScreenPixelsToMeters(
                                                     shake.selfAmplitudePixels, *pose, Owner()->Root().Position()),
                                                 .length = length,
                                                 .seed = seed,
                                                 .firstSign = firstSign,
                                                 .flipFrames = shake.flipFrames,
                                                 .ghostRatio = shake.ghostRatio},
                         .startClock = m_clock,
                         .active = true};
        // 始めたフレームから描く。時計の 0 の事象は時計を進めた後に起きるので、ここで書かないと 1 フレーム遅れる
        AdvanceBodyShake();

        // 相手は逆向きに揺れる。真後ろからは二人が重なるので、相手の欄を大きくすると誰が震えているかが分かる
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        NS::Obj::Actor* target = scene->Objects().FindObject(m_pendingTarget);
        if (target == nullptr)
        {
            return;
        }
        const TackleShakeDesc other{.axis = axis,
                                    .amplitude =
                                        ScreenPixelsToMeters(shake.amplitudePixels, *pose, target->Root().Position()),
                                    .length = length,
                                    .seed = seed,
                                    .firstSign = -firstSign,
                                    .flipFrames = shake.flipFrames,
                                    .ghostRatio = shake.ghostRatio};
        (void)SendMsgTackleShake(*target, other);
    }

    void ImpactResolver::StartShakeLines(const ShakeLinesEvent& lines, int length)
    {
        if (m_hitReaction == nullptr || Owner() == nullptr || length <= 0)
        {
            return;
        }
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        NS::Obj::Actor* target = scene->Objects().FindObject(m_pendingTarget);
        if (target == nullptr || target->ModelSubObj() == nullptr || m_player->ModelSubObj() == nullptr)
        {
            return;
        }
        // 止めの間の二人は食い込んだ所に留まり、描く形が揺れるだけなので、始めた時の形で挟めば最後まで外れない
        // 真後ろのカメラでは自機が相手に重なるので、二人をまとめて挟む
        const NS::AABB other = target->ModelSubObj()->WorldBounds();
        const NS::AABB self = m_player->ModelSubObj()->WorldBounds();
        m_hitReaction->StartShakeLines(
            NS::Obj::HitShakeLinesDesc{.center = NS::Vector3{other.Center.x, other.Center.y, other.Center.z},
                                       .radius = std::max({other.Extents.x, other.Extents.y, other.Extents.z}),
                                       .otherCenter = NS::Vector3{self.Center.x, self.Center.y, self.Center.z},
                                       .otherRadius = std::max({self.Extents.x, self.Extents.y, self.Extents.z}),
                                       .frames = length,
                                       .flipFrames = lines.flipFrames,
                                       .lengthPixels = lines.lengthPixels,
                                       .widthPixels = lines.widthPixels,
                                       .gapPixels = lines.gapPixels});
    }

    void ImpactResolver::AdvanceBodyShake()
    {
        if (!m_bodyShake.active || m_player->ModelSubObj() == nullptr)
        {
            return;
        }
        const TackleShakeDesc& desc = m_bodyShake.desc;
        const int frame = m_clock - m_bodyShake.startClock + 1;
        const float offset =
            BodyShakeOffset(frame, desc.length, desc.amplitude, desc.seed, desc.firstSign, desc.flipFrames);
        (void)m_player->ModelSubObj()->SetDrawOffset(desc.axis * offset);
        (void)m_player->ModelSubObj()->SetGhostSpread(
            desc.axis * (BodyShakeReach(frame, desc.length, desc.amplitude) * desc.ghostRatio));
        if (frame >= desc.length)
        {
            m_bodyShake.active = false;
        }
    }

    void ImpactResolver::StopBodyShake() noexcept
    {
        m_bodyShake.active = false;
        if (m_player != nullptr && m_player->ModelSubObj() != nullptr)
        {
            (void)m_player->ModelSubObj()->SetDrawOffset(NS::Vector3{0.0f, 0.0f, 0.0f});
            (void)m_player->ModelSubObj()->SetGhostSpread(NS::Vector3{0.0f, 0.0f, 0.0f});
        }
    }

    void ImpactResolver::StartTremor(const ImpactTremorEvent& tremor, int length)
    {
        StopTremor();
        if (Owner() == nullptr || length <= 0)
        {
            return;
        }
        // 震えは自機の玉が相手の表面に触れた点から両方の体へ伝わる
        const NS::Vector3 contact = m_lastImpact.surfacePoint;
        // 長さは止めに合わせて短くなる。裏まで届くのに長さを使い切ると震えが残らないので、届くのは長さの半分まで
        const int reachFrames = std::min(tremor.reachFrames, length / 2);
        m_tremor = TremorRun{.desc = TackleTremorDesc{.contactOffset = contact - Owner()->Root().Position(),
                                                      .amplitudePixels = tremor.amplitudePixels,
                                                      .reachFrames = reachFrames,
                                                      .length = length,
                                                      .referenceHeight = tremor.referenceHeight},
                             .startClock = m_clock,
                             .elapsed = 0,
                             .active = true};

        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        NS::Obj::Actor* target = scene->Objects().FindObject(m_pendingTarget);
        if (target == nullptr)
        {
            return;
        }
        TackleTremorDesc other = m_tremor.desc;
        other.contactOffset = contact - target->Root().Position();
        (void)SendMsgTackleTremor(*target, other);
    }

    void ImpactResolver::AdvanceTremor() noexcept
    {
        if (m_tremor.active)
        {
            m_tremor.elapsed = m_clock - m_tremor.startClock;
        }
    }

    void ImpactResolver::WriteTremor()
    {
        if (!m_tremor.active || Owner() == nullptr || m_player->ModelSubObj() == nullptr)
        {
            return;
        }
        const int elapsed = m_tremor.elapsed;
        NS::Gfx::TremorCB tremor{};
        const std::optional<NS::Obj::CameraPose> pose = NS::Obj::CameraViewPose(*Owner());
        if (pose.has_value())
        {
            // 自機の玉の差し渡しで裏まで届く
            tremor = MakeTremor(
                m_tremor.desc, elapsed, Owner()->Root().Position(), 2.0f * m_player->Collider().CapsuleRadius(), *pose);
        }
        (void)m_player->ModelSubObj()->SetTremor(tremor);
        if (elapsed >= m_tremor.desc.length)
        {
            m_tremor.active = false;
        }
    }

    void ImpactResolver::StartGroundWave(const GroundWaveEvent& wave, int length)
    {
        StopGroundWave();
        if (length <= 0)
        {
            return;
        }
        // 中心は自機の玉が相手の表面に触れた点。床の上の水平の位置だけを使う
        const NS::Vector3 contact = m_lastImpact.surfacePoint;
        m_groundWave = GroundWaveRun{.event = wave,
                                     .centerX = contact.x,
                                     .centerZ = contact.z,
                                     .length = length,
                                     .startClock = m_clock,
                                     .active = true};
        // 始めたフレームから描く。時計の事象は時計を進めた後に起きるので、ここで書かないと 1 フレーム遅れる
        AdvanceGroundWave();
    }

    void ImpactResolver::AdvanceGroundWave()
    {
        if (!m_groundWave.active || Owner() == nullptr || Owner()->OwningScene() == nullptr)
        {
            return;
        }
        const int elapsed = m_clock - m_groundWave.startClock;
        if (elapsed >= m_groundWave.length)
        {
            StopGroundWave();
            return;
        }
        const float frame = static_cast<float>(elapsed);
        Owner()->OwningScene()->SetGroundWave(
            NS::Gfx::GroundWave{.centerX = m_groundWave.centerX,
                                .centerZ = m_groundWave.centerZ,
                                .radius = m_groundWave.event.radius.Evaluate(frame),
                                .strength = m_groundWave.event.strength.Evaluate(frame)});
    }

    void ImpactResolver::StopGroundWave() noexcept
    {
        // 書いた時だけ消す。床の波を出していない裁定役が、他の物の波を消さない
        if (!m_groundWave.active)
        {
            return;
        }
        m_groundWave.active = false;
        if (Owner() != nullptr && Owner()->OwningScene() != nullptr)
        {
            Owner()->OwningScene()->SetGroundWave(NS::Gfx::GroundWave{});
        }
    }

    void ImpactResolver::StartDistortionRing(const DistortionRingEvent& ring, int length)
    {
        StopDistortionRing();
        if (length <= 0)
        {
            return;
        }
        // 中心は自機の玉が相手の表面に触れた点
        m_distortionRing = DistortionRingRun{.event = ring,
                                             .center = m_lastImpact.surfacePoint,
                                             .length = length,
                                             .startClock = m_clock,
                                             .active = true};
        // 始めたフレームから描く。時計の事象は時計を進めた後に起きるので、ここで書かないと 1 フレーム遅れる
        AdvanceDistortionRing();
    }

    void ImpactResolver::AdvanceDistortionRing()
    {
        if (!m_distortionRing.active || Owner() == nullptr || Owner()->OwningScene() == nullptr)
        {
            return;
        }
        const int elapsed = m_clock - m_distortionRing.startClock;
        if (elapsed >= m_distortionRing.length)
        {
            StopDistortionRing();
            return;
        }
        const float frame = static_cast<float>(elapsed);
        Owner()->OwningScene()->SetDistortionRing(
            NS::Gfx::DistortionRing{.center = m_distortionRing.center,
                                    .radius = m_distortionRing.event.radius.Evaluate(frame),
                                    .push = m_distortionRing.event.push.Evaluate(frame),
                                    .halfWidth = m_distortionRing.event.halfWidth});
    }

    void ImpactResolver::StopDistortionRing() noexcept
    {
        // 書いた時だけ消す。歪みの輪を出していない裁定役が、他の物の輪を消さない
        if (!m_distortionRing.active)
        {
            return;
        }
        m_distortionRing.active = false;
        if (Owner() != nullptr && Owner()->OwningScene() != nullptr)
        {
            Owner()->OwningScene()->SetDistortionRing(NS::Gfx::DistortionRing{});
        }
    }

    void ImpactResolver::StopTremor() noexcept
    {
        m_tremor.active = false;
        if (m_player != nullptr && m_player->ModelSubObj() != nullptr)
        {
            (void)m_player->ModelSubObj()->SetTremor(NS::Gfx::TremorCB{});
        }
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
        m_framesToPredictedContact = -1;
        // 相手へは明けを送らない。相手はやり直しの知らせで自分の位置へ戻り、凍結は相手の数えで明ける
        m_pendingTarget = NS::Obj::ActorRef{};
        if (hadStop && m_hitReaction != nullptr)
        {
            m_hitReaction->Stop();
        }
    }

    ImpactTuning MakeImpactTuning(const GL::Player::PlayerParams& params) noexcept
    {
        return ImpactTuning{.centerHitStopScale = params.m_centerHitStopScale,
                            .breakEnabled = params.m_breakEnabled,
                            .breakSpeedScale = params.m_breakSpeedScale,
                            .breakStopSeconds = params.m_breakStopSeconds,
                            .reboundDistance = params.m_reboundDistance,
                            .reboundApexHeight = params.m_reboundApexHeight,
                            .centerHitReboundDistanceScale = params.m_centerHitReboundDistanceScale,
                            .centerHitReboundHeightScale = params.m_centerHitReboundHeightScale,
                            .missReboundDistanceScale = params.m_missReboundDistanceScale,
                            .launchDistance = params.m_launchDistance,
                            .launchMassExponent = params.m_launchMassExponent,
                            .hitStopMaxSeconds = params.m_hitStopMaxSeconds,
                            .fixedDelta = NS::OS::FrameTimer::FixedDelta(),
                            .missReboundHeightRatio = params.m_missReboundHeightRatio,
                            .missSlamBounce = params.m_missSlamBounce,
                            .missBoxEdgeSharpness = params.m_missBoxEdgeSharpness};
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
                                        .tier = m_lastImpact.tier,
                                        .power = m_lastImpact.power,
                                        .launchScale = m_lastImpact.launchScale,
                                        .hopSeed = m_lastImpact.sequence};
        (void)SendMsgTackleRelease(*target, release);
    }

    NS::Vector3 ImpactResolver::ShapeFactors() const noexcept
    {
        // 事象の外は補間の残差を残さず、ちょうど 1 を返す
        if (!IsShapeAnimating())
        {
            return NS::Vector3{1.0f, 1.0f, 1.0f};
        }
        const float frame = static_cast<float>(m_clock - m_shapeStart);
        return AlongImpactFactors(
            CurveFactor(m_shape.along, frame), CurveFactor(m_shape.height, frame), CurveFactor(m_shape.side, frame));
    }

    NS::Vector3 ImpactResolver::AlongImpactFactors(float along, float height, float side) const noexcept
    {
        // 衝突は水平でしか起きない。進行の軸成分の 2 乗で倍率を混ぜ、軸に載った衝突では素の倍率になる
        // 横は進行に直角な水平の軸なので、x には z の成分の 2 乗、z には x の成分の 2 乗で混ぜる
        const float dx2 = m_pendingImpactDir.x * m_pendingImpactDir.x;
        const float dz2 = m_pendingImpactDir.z * m_pendingImpactDir.z;
        return NS::Vector3{1.0f + (along - 1.0f) * dx2 + (side - 1.0f) * dz2,
                           height,
                           1.0f + (along - 1.0f) * dz2 + (side - 1.0f) * dx2};
    }

    NS_CLASS(ImpactResolver)
} // namespace GL::Level
