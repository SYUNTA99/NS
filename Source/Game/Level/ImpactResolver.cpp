#include "Game/Level/ImpactResolver.h"

#include "Game/Level/HitZones.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "Game/Player/LaunchPitch.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/SlamBall.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"
#include "NSlib/Phys/Capsule.h"
#include "NSlib/Windows/Clock.h"

#include "NSlib/Core/Types.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace GL
{
    namespace Level
    {
        namespace
        {
            // 自機の位置から向きの線を引いた時の、相手の外接箱の中心の測り
            struct LineOffset
            {
                float along = 0.0f; // 自機の位置から相手の中心までの、線に沿った水平の距離 (m)。後ろは負
                float ratio = 0.0f; // 線から相手の中心までの横ずれ ÷ (相手の半幅 + 自機の半径)。0〜1 へ丸めない
            };

            // 探索の絞りに使う AABB の横ずれ比。実際の段は JudgeHitFace で決める
            // 半幅と自機半径の和を 1 とする。球や傾いた箱では実際の縁より広い
            // 前提: direction の水平成分が非零
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
            [[nodiscard]] float FirstTouchDistance(const NS::SensorVolume& target,
                                                   const NS::Vector3& origin,
                                                   const NS::Vector3& direction,
                                                   float distance,
                                                   float radius,
                                                   float tolerance) noexcept
            {
                const auto touches = [&](float length) {
                    return NS::VolumesOverlap(NS::SensorVolume::Capsule(SweptBall(origin, direction, length, radius)),
                                              target);
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
            [[nodiscard]] NS::Vector3 PositionAfterStep(const NS::Vector3& position,
                                                        const NS::Vector3& velocity) noexcept
            {
                const float dt = NS::OS::FrameTimer::FixedDelta();
                return NS::Vector3{
                    position.x + velocity.x * dt, position.y + velocity.y * dt, position.z + velocity.z * dt};
            }

            // 当たりの止めのフレーム数。自機の段の束の止めの長さで、束が無ければ 0
            // 貫通は欄「貫通の止め秒」の長さで、自機が束を始めた後に止めの残りを上書きする
            [[nodiscard]] int HitStopStepsFor(const NS::HitReaction* reaction,
                                              std::string_view setName,
                                              const ImpactOutcome& outcome) noexcept
            {
                if (outcome.broke)
                {
                    return std::max(outcome.stopSteps, 0);
                }
                if (reaction == nullptr)
                {
                    return 0;
                }
                const NS::HitReactionSet* set = reaction->FindSet(setName);
                if (set == nullptr)
                {
                    return 0;
                }
                return set->stopFrames;
            }

            // 明けに紫の明けの束で世界を遅くするか。溜めすぎて放した突進の当たりで、段にその束がある時だけ
            [[nodiscard]] bool UsesGradualRelease(const NS::HitReaction* reaction, HitTier tier, float overcharge01)
            {
                if (!(overcharge01 > 0.0f) || reaction == nullptr)
                {
                    return false;
                }
                return reaction->FindSet(TackleSetName("明け", tier, "・紫")) != nullptr;
            }

            // 体当たりの相手を決める唯一の所。置物の体だけを相手にする
            // 有効か・自分かの絞りは FindOverlaps が持つ。当たりの裁定と狙う相手の探索が同じ絞りを通る
            [[nodiscard]] bool IsTackleTarget(const NS::HitSensor& sensor) noexcept
            {
                return IsSensorKind(&sensor, SensorKind::MapObjBody);
            }
        } // namespace

        NS::HitSensor* ImpactResolver::FindOverlapped(const ImpactSelf& self,
                                                      const NS::Vector3& predictedVelocity,
                                                      const NS::Collider* collider) const
        {
            NS_ASSERT(Game, collider != nullptr, "FindOverlapped: 渡したアクターか部品が無い");
            if (self.sensors == nullptr)
            {
                return nullptr;
            }

            const NS::Vector3 position = self.position;

            // この固定ステップで進んだ先で見る。今の位置だけでは手前で止められて重ならず、反発が起きない
            const NS::Phys::Capsule capsule = collider->CapsuleAt(PositionAfterStep(position, predictedVelocity));
            const std::vector<NS::HitSensor*> touching =
                self.sensors->FindOverlaps(NS::SensorVolume::Capsule(capsule), self.ignore);

            NS::HitSensor* nearest = nullptr;
            float nearestDistanceSq = 0.0f;
            for (NS::HitSensor* sensor : touching)
            {
                if (!IsTackleTarget(*sensor))
                {
                    continue;
                }
                const NS::AABB bounds = sensor->WorldVolume(sensor->Host()->Root().WorldMatrix()).Bounds();
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

        bool ImpactResolver::FindSlamLineTarget(const ImpactSelf& self,
                                                const NS::Vector3& direction,
                                                float maxDistance,
                                                const GL::Player::PlayerParams* params,
                                                const NS::Body* body,
                                                const NS::Collider* collider,
                                                SlamLineTarget& outTarget) const
        {
            NS_ASSERT(Game,
                      params != nullptr && body != nullptr && collider != nullptr,
                      "自機の調整値・体・当たりの部品のどれかが渡されていない");
            NS::Vector3 lineDir{};
            if (!NS::TryNormalizeHorizontal(direction, lineDir))
            {
                return false;
            }
            // 非数と無限の向きは正規化を通り抜ける
            if (!NS::IsFinite(lineDir))
            {
                return false;
            }
            if (self.sensors == nullptr)
            {
                return false;
            }

            if (!(maxDistance >= 0.0f) || !std::isfinite(maxDistance))
            {
                return false;
            }

            const NS::Vector3 position = self.position;
            const float playerRadius = collider->CapsuleRadius();
            // 突進は丸まった玉で進む。玉の決まりは SlamBallAt が持つ
            const NS::Vector3 ballCenter = GL::Player::SlamBallAt(collider, position).center;
            // 届くかは裁定と同じく、自機の当たりの玉と相手の体のセンサーの形で見る。外接箱を水平に見ると、中心の高い
            // 大きな球の端では、玉が触れずに横を通るのに届くと出る
            const NS::SensorVolume swept =
                NS::SensorVolume::Capsule(SweptBall(ballCenter, lineDir, maxDistance, playerRadius));

            bool found = false;
            SlamLineTarget first{};
            NS::HitSensor* firstSensor = nullptr;
            // 掃いた玉に触れる相手だけを先に問う。下の線の条件はどれも順に依らないので、選ぶ相手は絞る順で変わらない
            for (NS::HitSensor* sensor : self.sensors->FindOverlaps(swept, self.ignore))
            {
                if (!IsTackleTarget(*sensor))
                {
                    continue;
                }
                const NS::SensorVolume volume = sensor->WorldVolume(sensor->Host()->Root().WorldMatrix());
                const NS::AABB bounds = volume.Bounds();

                const LineOffset line = MeasureLineOffset(position, bounds, lineDir, playerRadius);
                if (!(line.along > 0.0f))
                {
                    continue;
                }
                // 比が 1 を超える相手は、線を進む自機の縁が相手の外接箱の縁に届かない。丸めた値で見ると 1
                // に張り付いて拾う
                if (!(line.ratio <= 1.0f))
                {
                    continue;
                }
                // 最初に触れる相手は、中心の近さでなく玉が触れるまでに進む距離で決める。突進はそこで止まって当たる
                const float contact = FirstTouchDistance(
                    volume, ballCenter, lineDir, maxDistance, playerRadius, std::max(params->m_contactTolerance, 0.0f));
                if (!found || contact < first.contact)
                {
                    found = true;
                    first = SlamLineTarget{.target = sensor->Host(),
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
            if (SendMsgAskTackleTarget(firstSensor, answer))
            {
                float aimHeight = ballCenter.y;
                (void)HitFaceAimHeight(answer.face, answer.body, lineDir, playerRadius, aimHeight);
                // 触れる所は、着きたい高さで線を進めた玉が触れる所。今の高さで測ると、高さの違う相手の上の縁をかすめる
                // 所まで寄ってしまい、弧の着く所と実際に触れる所がずれる。その高さで触れない時は今の高さの値のまま
                const NS::Vector3 aimedCenter{ballCenter.x, aimHeight, ballCenter.z};
                float aimedContact = first.contact;
                if (NS::VolumesOverlap(
                        NS::SensorVolume::Capsule(SweptBall(aimedCenter, lineDir, maxDistance, playerRadius)),
                        firstSensor->WorldVolume(firstSensor->Host()->Root().WorldMatrix())))
                {
                    aimedContact = FirstTouchDistance(firstSensor->WorldVolume(firstSensor->Host()->Root().WorldMatrix()),
                                                      aimedCenter,
                                                      lineDir,
                                                      maxDistance,
                                                      playerRadius,
                                                      std::max(params->m_contactTolerance, 0.0f));
                }
                const float dt = NS::OS::FrameTimer::FixedDelta();
                const GL::Player::LaunchPitchResult pitch = GL::Player::LaunchPitch(
                    GL::Player::LaunchPitchDesc{.ballHeight = ballCenter.y,
                                                .targetHeight = aimHeight,
                                                .contactDistance = aimedContact,
                                                .horizontalSpeed = params->m_bodySlamSpeed,
                                                .gravity = params->Gravity(),
                                                .maxAngleDegrees = params->m_launchPitchLimitDegrees,
                                                .grounded = body->IsGrounded(),
                                                .dt = dt,
                                                .heightTolerance = params->m_launchHeightTolerance,
                                                .angleGuardDegrees = params->m_launchAngleGuardDegrees,
                                                .maxFrames = params->m_launchMaxFrames});
                first.launchVerticalSpeed = pitch.verticalSpeed;
                if (pitch.reachable)
                {
                    first.launchContact = aimedContact;
                }
                // 段と横ずれは裁定と同じく相手の答えの面で決める。玉の高さは放つ縦の速さの道筋が触れる所で居る高さ
                const GL::Player::LaunchPath path{.horizontalSpeed = params->m_bodySlamSpeed,
                                                  .verticalSpeed = pitch.verticalSpeed,
                                                  .gravity = params->Gravity(),
                                                  .dt = dt,
                                                  .grounded = body->IsGrounded(),
                                                  .maxFrames = params->m_launchMaxFrames};
                const NS::Vector3 arrival{
                    ballCenter.x, ballCenter.y + GL::Player::LaunchHeightAt(path, first.launchContact), ballCenter.z};
                judgement = JudgeHitFaceOrWide(answer.face, answer.body, arrival, lineDir, playerRadius);
            }
            first.offset = judgement.offset01;
            first.tier = judgement.tier;
            outTarget = first;
            return true;
        }

        void ImpactResolver::ObserveImpact(const ImpactSelf& self,
                                           const BodySlamMotion& motion,
                                           const NS::Collider* collider)
        {
            NS_ASSERT(Game, collider != nullptr, "当たりの部品が渡されていない");
            m_stateReady = true;
            m_hasObservedTarget = false;
            if (!motion.slamming)
            {
                return;
            }
            const NS::Vector3 predictedVelocity = motion.velocity;
            NS::HitSensor* hit = FindOverlapped(self, predictedVelocity, collider);
            if (hit == nullptr || hit->Host() == nullptr)
            {
                return;
            }
            TackleTargetAnswer answer{};
            if (!SendMsgAskTackleTarget(hit, answer))
            {
                return;
            }
            m_observedTarget = hit->Host();
            m_observedAnswer = answer;
            m_observedVelocity = predictedVelocity;
            m_hasObservedTarget = true;
        }

        void ImpactResolver::StepState(const ImpactSelf& self,
                                       const BodySlamMotion& motion,
                                       const GL::Player::PlayerParams* params,
                                       const NS::Body* body,
                                       const NS::Collider* collider,
                                       const NS::HitReaction* hitReaction)
        {
            NS_ASSERT(Game,
                      params != nullptr && body != nullptr && collider != nullptr,
                      "自機の調整値・体・当たりの部品のどれかが渡されていない");
            ClearOrders();
            AdvanceState(
                self,
                motion,
                PlayerParts{
                    .params = params, .body = body, .collider = collider, .hitReaction = hitReaction});
        }

        void ImpactResolver::Release()
        {
            ApplyRebound();
            LaunchTarget();
        }

        void ImpactResolver::AdvanceState(const ImpactSelf& self,
                                          const BodySlamMotion& motion,
                                          const PlayerParts& parts)
        {
            if (!m_stateReady)
            {
                return;
            }
            m_stateReady = false;
            const bool hadObservation = m_hasObservedTarget;
            m_hasObservedTarget = false;
            m_answer.rebounded = false;
            m_answer.broke = false;
            m_answer.framesToPredictedContact = -1;
            m_answer.predicted.reset();
            // 検知の次のフレームに、自機が当たりの状態へ移って段の束を始める
            if (m_hitAwaitsEntry)
            {
                m_hitAwaitsEntry = false;
                m_answer.beginHit = true;
            }

            // 押していない接触は物理の停止だけで済ませるため、体当たり中でないフレームは裁定しない
            // 当たりの状態の自機は突進を打ち切っているので、止めの間に重なったままの相手を検知し直さない
            if (!motion.slamming)
            {
                return;
            }
            if (ResolveObservedHit(self, hadObservation, motion, parts))
            {
                return;
            }
            PredictContact(self, motion, parts);
        }

        void ImpactResolver::ClearOrders() noexcept
        {
            // Player は答えを読んだフレームにする。次のフレームへ残すと、同じ事を 2 回する
            m_answer.cancelBodySlam = false;
            m_answer.beginHit = false;
            m_answer.releaseVelocity.reset();
            m_answer.reboundArc.reset();
        }

        bool ImpactResolver::ResolveObservedHit(const ImpactSelf& self,
                                                bool hadObservation,
                                                const BodySlamMotion& motion,
                                                const PlayerParts& parts)
        {
            if (!hadObservation || self.sensors == nullptr)
            {
                return false;
            }
            if (!m_observedTarget->IsAppeared())
            {
                return false;
            }
            const TackleTargetAnswer& answer = m_observedAnswer;
            const NS::AABB bounds = answer.bounds;

            const NS::Vector3 position = self.position;
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

            const float charge01 = motion.charge01;

            const float mass = answer.mass;

            // 重なりと面の判定を同じステップ先で見る。現在位置を使うと、縦移動中の段がずれる
            const NS::Vector3 stepped = PositionAfterStep(position, velocity);
            const NS::Vector3 ballCenter = GL::Player::SlamBallAt(parts.collider, stepped).center;
            const HitFaceJudgement judgement =
                JudgeHitFaceOrWide(answer.face, answer.body, ballCenter, velocity, parts.collider->CapsuleRadius());
            const float offset01 = judgement.offset01;
            const float chargeFactor = parts.params->ChargeFactorFor(charge01, motion.overcharge01);
            const float positionFactor = judgement.powerScale;
            const HitTier tier = judgement.tier;
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

            // 明けたフレームの反発と貫通速度を通常移動に乗せるため、凍結より先に突進を打ち切る。打ち切るのは Player
            m_answer.cancelBodySlam = true;

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
            const ImpactOutcome outcome = ComputeImpactOutcome(impactInput, MakeImpactTuning(parts.params));

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
                m_answer.broke = true;
                NS_LOG_INFO(Game, "貫通: 耐久 {} 威力 {} 中心近く {}", answer.toughness, power, centerHit);
            }
            else
            {
                m_pendingBreak = false;
                m_pendingReboundArc = outcome.reboundArc;
                m_pendingSelfVelocity = LaunchArcInitialVelocity(parts.params->ReboundLaunchArc(m_pendingReboundArc));
                m_pendingLaunchArc = outcome.launchArc;

                m_answer.rebounded = true;
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

            const std::string setName = TackleSetName("体当たり", tier);
            const int stopSteps = HitStopStepsFor(parts.hitReaction, setName, outcome);
            const bool gradualRelease = UsesGradualRelease(parts.hitReaction, tier, motion.overcharge01);

            // 止めるフレーム数が決まってから控える
            m_answer.lastImpact.sequence += 1;
            m_answer.lastImpact.targetId = m_pendingTarget->Id();
            m_answer.lastImpact.power = power;
            m_answer.lastImpact.charge01 = charge01;
            m_answer.lastImpact.positionFactor = positionFactor;
            m_answer.lastImpact.offset01 = offset01;
            m_answer.lastImpact.tier = tier;
            m_answer.lastImpact.setName = setName;
            m_answer.lastImpact.faceU = judgement.u;
            m_answer.lastImpact.faceV = judgement.v;
            m_answer.lastImpact.faceNormal = outcome.faceNormal;
            m_answer.lastImpact.hitStopSteps = stopSteps;
            m_answer.lastImpact.localStop = stopSteps > 0;
            m_answer.lastImpact.overcharge01 = motion.overcharge01;
            m_answer.lastImpact.gradualRelease = gradualRelease;
            m_answer.lastImpact.centerHit = centerHit;
            m_answer.lastImpact.broke = m_pendingBreak;
            m_answer.lastImpact.selfVelocity = m_pendingSelfVelocity;
            m_answer.lastImpact.reboundApexHeight = m_pendingReboundArc.apexHeight;
            m_answer.lastImpact.launchVelocity = LaunchArcInitialVelocity(m_pendingLaunchArc);
            m_answer.lastImpact.launchDistance = m_pendingLaunchArc.distance;
            m_answer.lastImpact.launchApexHeight = m_pendingLaunchArc.apexHeight;
            m_answer.lastImpact.impactDir = m_pendingImpactDir;
            // 触れた点は記録とエディタの印だけが読む。段と威力を決めた判定の結果を使う
            m_answer.lastImpact.surfacePoint = judgement.surfacePoint;
            m_answer.lastImpact.targetPos = answer.position;
            m_answer.lastImpact.targetBottom = bounds.Center.y - bounds.Extents.y;
            m_answer.lastImpact.targetMass = mass;
            m_answer.lastImpact.targetPlaced = answer.placed;
            m_answer.lastImpact.launchScale = launchScale;
            m_answer.lastImpact.reboundScale = reboundScale;

            m_hitAwaitsEntry = true;
            return true;
        }

        void ImpactResolver::PredictContact(const ImpactSelf& self,
                                            const BodySlamMotion& motion,
                                            const PlayerParts& parts)
        {
            if (!motion.predictsContact)
            {
                return;
            }
            const NS::Vector3 velocity = motion.velocity;
            NS::Vector3 direction{};
            const float stepLength =
                std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z) * NS::OS::FrameTimer::FixedDelta();
            SlamLineTarget predicted{};
            if (!NS::TryNormalizeHorizontal(velocity, direction) || !(stepLength > 0.0f) ||
                !FindSlamLineTarget(
                    self,
                    direction, motion.remainingDistance, parts.params, parts.body, parts.collider, predicted))
            {
                return;
            }
            // 観測の段は、今の位置から 1 歩進んだ玉の重なりで当たりを見る。触れるまでの距離を k 歩目に越えるなら、
            // 検知はこのフレームから k - 1 フレーム後。このフレームで検知しなかったので 1 以上
            m_answer.framesToPredictedContact =
                std::max(static_cast<int>(std::ceil(predicted.contact / stepLength)) - 1, 1);
            m_answer.predicted = std::move(predicted);
        }

        void ImpactResolver::CancelImpact() noexcept
        {
            m_hitAwaitsEntry = false;
            m_pendingBreak = false;
            m_hasObservedTarget = false;
            m_stateReady = false;
            m_answer.rebounded = false;
            m_answer.broke = false;
            m_answer.framesToPredictedContact = -1;
            m_answer.predicted.reset();
            ClearOrders();
            // 相手へは明けを送らない。相手はやり直しの知らせで自分の位置へ戻り、凍結は相手の数えで明ける
            m_pendingTarget = nullptr;
        }

        ImpactTuning MakeImpactTuning(const GL::Player::PlayerParams* params) noexcept
        {
            NS_ASSERT(Game, params != nullptr, "MakeImpactTuning: 渡したアクターか部品が無い");
            return ImpactTuning{.centerHitStopScale = params->m_centerHitStopScale,
                                .breakEnabled = params->m_breakEnabled,
                                .breakSpeedScale = params->m_breakSpeedScale,
                                .breakStopSeconds = params->m_breakStopSeconds,
                                .reboundDistance = params->m_reboundDistance,
                                .reboundApexHeight = params->m_reboundApexHeight,
                                .centerHitReboundDistanceScale = params->m_centerHitReboundDistanceScale,
                                .centerHitReboundHeightScale = params->m_centerHitReboundHeightScale,
                                .missReboundDistanceScale = params->m_missReboundDistanceScale,
                                .launchDistance = params->m_launchDistance,
                                .launchMassExponent = params->m_launchMassExponent,
                                .hitStopMaxSeconds = params->m_hitStopMaxSeconds,
                                .fixedDelta = NS::OS::FrameTimer::FixedDelta(),
                                .missReboundHeightRatio = params->m_missReboundHeightRatio,
                                .missSlamBounce = params->m_missSlamBounce,
                                .missBoxEdgeSharpness = params->m_missBoxEdgeSharpness};
        }

        void ImpactResolver::ApplyRebound()
        {
            m_answer.releaseVelocity = m_pendingSelfVelocity;
            if (!m_pendingBreak)
            {
                m_answer.reboundArc = m_pendingReboundArc;
            }
        }

        void ImpactResolver::LaunchTarget()
        {
            // やり直しで当たりを捨てた後は知らせる相手が無い
            if (m_pendingTarget == nullptr)
            {
                return;
            }
            // 元の位置と形へ戻り、跡を残し、飛ぶか壊れるかは相手が決める
            const TackleReleaseDesc release{.arc = m_pendingLaunchArc,
                                            .breaks = m_pendingBreak,
                                            .tier = m_answer.lastImpact.tier,
                                            .power = m_answer.lastImpact.power,
                                            .launchScale = m_answer.lastImpact.launchScale,
                                            .hopSeed = m_answer.lastImpact.sequence};
            (void)SendMsgTackleRelease(m_pendingTarget, release);
        }
    } // namespace Level
} // namespace GL
