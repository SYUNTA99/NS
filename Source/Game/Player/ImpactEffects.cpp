#include "Game/Player/ImpactEffects.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"

#include "Game/Level/ImpactResolver.h"
#include "Game/Player/PlayerStatus.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/CoreObject/IGet/IGetEffect.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace GL
{
    namespace Player
    {
        namespace
        {
            using NS::NormalizedOr;
            using NS::Quaternion;
            using NS::TurnNormalTo;
            using NS::TurnUpTo;
            using NS::Vector3;
            using NS::Gfx::MakeEffectPlayDesc;

            [[nodiscard]] NS::Gfx::EffectPlayDesc PlayAt(const Vector3& position,
                                                         const Quaternion& rotation,
                                                         const Vector3& scale) noexcept
            {
                return MakeEffectPlayDesc(position, rotation, scale);
            }

            [[nodiscard]] Vector3 Uniform(float value) noexcept
            {
                return NS::UniformScale(value);
            }

        } // namespace

        void ImpactEffects::Preload(const NS::IGetEffect* effect)
        {
            NS_ASSERT(Game, effect != nullptr, "ImpactEffects::Preload: 絵を出すアクターが空");

            NS::PreloadEffects(effect,
                               {m_coreAsset,
                                m_streakAsset,
                                m_ringAsset,
                                m_sparksAsset,
                                m_embersAsset,
                                m_glowAsset,
                                m_recoilAsset,
                                m_dustAsset,
                                m_reboundTrailAsset,
                                m_landDustAsset,
                                m_burstAsset,
                                m_slamTrailAsset});
        }

        void ImpactEffects::UpdateSlam(const NS::IGetEffect* user,
                                       const NS::Vector3& center,
                                       const BodySlamStatus& slam,
                                       const PlayerMotion& motion,
                                       const GL::Level::ImpactAnswer& impact)
        {
            NS_ASSERT(Game, user != nullptr, "絵を出すアクターが空");
            for (NS::uint32* id : {&m_burst, &m_slamTrail})
            {
                const NS::EffectRecord* record = NS::FindEffect(user, *id);
                if (record != nullptr && record->endStep.has_value())
                {
                    *id = 0;
                }
            }
            if (motion.bodySlamming)
            {
                m_slamDirection = slam.Direction();
            }
            Quaternion rotation = Quaternion::Identity;
            if (NS::HasPositiveMagnitudeHorizontal(m_slamDirection))
            {
                rotation = Quaternion::CreateFromAxisAngle(Vector3{0.0f, 1.0f, 0.0f},
                                                            std::atan2(m_slamDirection.x, m_slamDirection.z));
            }
            if (motion.bodySlamming && !m_wasSlamming)
            {
                NS::Gfx::EffectPlayDesc desc = PlayAt(center, rotation, Uniform(1.0f));
                desc.dynamicInputs[0] = slam.Charge01();
                m_burst = NS::PlayEffect(user, m_burstAsset, desc);
                NS::StopEffectAfter(user, m_burst, std::max(m_burstSteps, 1));
                NS::StopEffectRoot(user, m_slamTrail);
                NS::StopEffectAfter(user, m_slamTrail, std::max(m_slamTrailFadeSteps, 1));
                m_slamTrail = NS::PlayEffect(user, m_slamTrailAsset, desc);
            }
            const NS::EffectRecord* trail = NS::FindEffect(user, m_slamTrail);
            if (trail != nullptr && !trail->rootStopStep.has_value())
            {
                if (impact.beginHit || (!motion.bodySlamming && !impact.rebounded && !impact.broke))
                {
                    NS::StopEffectRoot(user, m_slamTrail);
                    NS::StopEffectAfter(user, m_slamTrail, std::max(m_slamTrailFadeSteps, 1));
                }
                else
                {
                    NS::SetEffectRotation(user, m_slamTrail, rotation);
                    NS::SetEffectTransform(user, m_slamTrail, center, rotation, Uniform(1.0f));
                }
            }
            const bool contactNear = impact.framesToPredictedContact >= 0 &&
                                     impact.framesToPredictedContact <= std::max(m_burstClearFrames, 0);
            if (m_burst != 0 && (motion.beforeContact || impact.rebounded || impact.broke || contactNear))
            {
                NS::StopEffect(user, m_burst);
                m_burst = 0;
            }
            m_wasSlamming = motion.bodySlamming;
        }

        void ImpactEffects::Reset(const NS::IGetEffect* user)
        {
            NS_ASSERT(Game, user != nullptr, "絵を出したアクターが空");
            ClearSlam(user);
            const NS::EffectObj* effects = user->GetEffect();
            if (effects != nullptr)
            {
                for (const NS::EffectRecord& record : effects->Records())
                {
                    NS::StopEffect(user, record.id);
                }
            }
            m_plan = HitPlan{};
            m_flight = Flight{};
            m_dusts.clear();
            m_hitRequested = false;
            m_flightRequested = false;
            m_lastVerticalVelocity = 0.0f;
            m_landingDustPlayed = false;
            m_aim = ImpactAim{};
        }

        void ImpactEffects::ClearSlam(const NS::IGetEffect* user)
        {
            NS_ASSERT(Game, user != nullptr, "絵を出したアクターが空");
            NS::StopEffect(user, m_slamTrail);
            NS::StopEffect(user, m_burst);
            m_slamTrail = 0;
            m_burst = 0;
            m_wasSlamming = false;
            m_slamDirection = Vector3::Zero;
        }

        void ImpactEffects::Update(const NS::IGetEffect* effect,
                                   const NS::IGetCamera* camera,
                                   const NS::Vector3& position,
                                   const BodySlamStatus& slam,
                                   const PlayerMotion& motion,
                                   const NS::Body* body,
                                   const NS::Collider* collider,
                                   const GL::Level::ImpactRecord& impact)
        {
            NS_ASSERT(Game, effect != nullptr && camera != nullptr, "絵を出すアクターかカメラを引くアクターが空");
            NS_ASSERT(Game, body != nullptr && collider != nullptr, "体か当たりの部品が渡されていない");
            const Owner owner{.effect = effect, .camera = camera, .position = position};
            if (m_hitRequested)
            {
                m_hitRequested = false;
                BeginHit(owner, impact, slam, collider);
            }
            if (m_flightRequested)
            {
                m_flightRequested = false;
                if (m_plan.active)
                {
                    m_plan.releaseStep = NS::GetEffectStep(owner.effect);
                    BeginFlight(owner, motion, body, impact);
                }
            }
            AdvanceHit(owner, collider);
            AdvanceFlight(owner, motion, body);
            AdvanceLanding(owner, motion, body, collider);
        }

        // 入力の番号は段の番号と別に持つ。入力3は威力なので、段の追加で上書きしない
        void ImpactEffects::ApplyTierRow(ImpactShape& shape,
                                         const GL::Level::ImpactRecord& impact) const noexcept
        {
            switch (impact.tier)
            {
            case GL::Level::HitTier::Center:
            {
                shape.coreInput = 0;
                // 核の留まりは止めと結ぶ。止めの短い当たりは明けの前に落ち始め、明けの後に光が残らない
                shape.holdLastFrame = std::max(1, std::min(m_coreHoldMax, impact.hitStopSteps - 1));
                shape.sparkStartFrame = std::max(m_centerSparkStart, 0);
                // 手本の弾きは光の塊が消える 11 に火の粉が接触点から新しく弾ける。核が落ちるフレームに揃える
                shape.emberStartFrame = shape.holdLastFrame + 1;
                shape.sparkHeading = SparkHeading::Launch;
                shape.centerLayers = true;
                // 照りは相手の足元の床に出す。飛んでいた相手の下に床は無い
                if (impact.targetPlaced)
                {
                    shape.groundLight = true;
                }
                return;
            }
            case GL::Level::HitTier::Wide:
                break;
            }
            // 大きな外れの行。番号から作った段の外の値もこの行で出し、中心近くの層を足さない
            shape.coreInput = 2;
            shape.holdLastFrame = std::max(m_wideCoreHoldLast, 0);
            shape.coreCut = true;
            shape.sparkHeading = SparkHeading::Scrape;
        }

        ImpactShape ImpactEffects::ShapeFor(const GL::Level::ImpactRecord& impact) const noexcept
        {
            ImpactShape shape;
            shape.tier = impact.tier;
            const float power = std::max(impact.power, 0.0f);
            shape.power = power;
            shape.launchScale = std::max(impact.launchScale, 0.0f);
            shape.reboundScale = std::max(impact.reboundScale, 0.0f);

            ApplyTierRow(shape, impact);

            // 粉は相手の足元の床に出す。飛んでいた相手の下に床は無い
            if (impact.targetPlaced)
            {
                shape.groundDust = true;
                shape.rootMass = std::sqrt(std::max(impact.targetMass, 0.0f));
            }

            return shape;
        }

        Vector3 ImpactEffects::MissSparkHeading(float u,
                                                float v,
                                                const Vector3& surfaceNormal,
                                                const Vector3& slamDirection) const noexcept
        {
            Vector3 forward{};
            if (!NS::TryNormalizeHorizontal(slamDirection, forward))
            {
                return Vector3{};
            }
            const Vector3 right{forward.z, 0.0f, -forward.x};
            const Vector3 side = NS::NormalizedOr(right * u + Vector3{0.0f, v, 0.0f}, Vector3{});
            if (!(side.Length() > 0.5f))
            {
                return Vector3{};
            }
            const Vector3 slide = NS::NormalizedOr(forward - surfaceNormal * NS::Dot(forward, surfaceNormal), side);
            const float sideShare = std::clamp(m_missSparkSideShare, 0.0f, 1.0f);
            return NS::NormalizedOr(side * sideShare + slide * (1.0f - sideShare), side);
        }

        Vector3 ImpactEffects::ReboundTrailHeading(const Vector3& velocity,
                                                   const Vector3& ballCenter,
                                                   const std::optional<Vector3>& cameraPosition) const noexcept
        {
            const Vector3 heading = NS::NormalizedOr(velocity, Vector3{0.0f, 1.0f, 0.0f});
            if (!cameraPosition.has_value())
            {
                return heading;
            }
            const Vector3 sight = NS::NormalizedOr(ballCenter - cameraPosition.value(), Vector3{});
            if (!(sight.Length() > 0.5f))
            {
                return heading;
            }
            // 筋は飛ぶ向きの逆へ伸びる。視線と直角な成分が画面の上に写る向き、視線に沿う成分は奥行き
            const Vector3 back = heading * -1.0f;
            const float along = NS::Dot(back, sight);
            const Vector3 across = back - sight * along;
            const float acrossLength = across.Length();
            // 真っ直ぐ視線に沿って飛ぶ時は画面の上の向きが決まらないので起こさない
            const float minAcross = std::clamp(m_reboundStreakMinAcross, 0.0f, 1.0f);
            if (acrossLength >= minAcross || !NS::HasPositiveMagnitude(acrossLength))
            {
                return heading;
            }
            // 画面の上の向きと、奥か手前かは保つ
            float keptAlong = std::sqrt(1.0f - minAcross * minAcross);
            if (along < 0.0f)
            {
                keptAlong = -keptAlong;
            }
            const Vector3 leaned = sight * keptAlong + across * (minAcross / acrossLength);
            return leaned * -1.0f;
        }

        void ImpactEffects::BeginHit(const Owner& owner,
                                     const GL::Level::ImpactRecord& impact,
                                     const BodySlamStatus& slam,
                                     const NS::Collider* collider)
        {
            // 前の当たりの層が残っていても、次の核は当たりの絵の頭から出す。前の粉は親を止めて短くする
            FinishHeldEffects(owner);
            // 頂点の前に次に当てたら、その反動はここで終わる。尾は輪ごと消す
            if (m_flight.reboundTrail != 0)
            {
                NS::StopEffect(owner.effect, m_flight.reboundTrail);
                m_flight.reboundTrail = 0;
            }
            for (const NS::uint32 dust : m_dusts)
            {
                NS::StopEffectRoot(owner.effect, dust);
            }
            m_dusts.clear();

            HitPlan plan;
            plan.active = true;
            plan.freezeStep = NS::GetEffectStep(owner.effect);
            plan.shape = ShapeFor(impact);
            plan.targetId = impact.targetId;

            const Vector3 self = owner.position;
            const float radius = collider->CapsuleRadius();
            const Vector3 horizontalForward{impact.impactDir.x, 0.0f, impact.impactDir.z};
            plan.launchDir = NS::NormalizedOr(horizontalForward, Vector3{1.0f, 0.0f, 0.0f});
            const Vector3 toTarget = NS::NormalizedOr(impact.targetPos - self, plan.launchDir);
            plan.contact = self + toTarget * radius;
            plan.awayDir = NS::NormalizedOr(Vector3{toTarget.x, 0.0f, toTarget.z}, plan.launchDir);

            // 横ずれの側: 相手の中心から自機への向きのうち、飛ぶ向きに直角な水平の成分
            const Vector3 away = self - impact.targetPos;
            Vector3 across = away - plan.launchDir * NS::Dot(away, plan.launchDir);
            across.y = 0.0f;
            const Vector3 fallbackSide = NS::Cross(Vector3{0.0f, 1.0f, 0.0f}, plan.launchDir);
            plan.sideDir = NS::NormalizedOr(across, NS::NormalizedOr(fallbackSide, Vector3{0.0f, 0.0f, 1.0f}));
            plan.scrapeDir = NS::NormalizedOr(plan.sideDir + plan.launchDir * m_scrapeLaunchWeight +
                                                  Vector3{0.0f, m_scrapeLiftWeight, 0.0f},
                                              plan.sideDir);
            // 外れは、どこで、どっちへ力がそれたかを読ませる。面の上の位置から外した側を取り、段と逸れ方と向きを揃える
            // 位置の無い当たり (面で判定できない体) は前の擦れの向きのまま
            const Vector3 missHeading =
                MissSparkHeading(impact.faceU, impact.faceV, impact.faceNormal, slam.Direction());
            if (missHeading.Length() > 0.5f)
            {
                plan.scrapeDir = missHeading;
            }
            plan.selfDir = NS::NormalizedOr(impact.selfVelocity, Vector3{0.0f, 1.0f, 0.0f});

            // 輪の法線は相手の飛ぶ向きを残しつつカメラへ起こす。真横から見て縦の線に潰れない
            Vector3 toCamera{};
            const std::optional<Vector3> camera = CameraPosition(owner);
            if (camera.has_value())
            {
                toCamera = NS::NormalizedOr(camera.value() - plan.contact, Vector3{});
            }
            plan.ringNormal = NS::NormalizedOr(plan.launchDir * m_ringLaunchWeight +
                                                   toCamera * m_ringFaceCamera,
                                               plan.launchDir);

            // 床は相手の体の外接箱の底。置かれた相手は床に接している。底は当てた時に相手が答えた値
            plan.floor = Vector3{impact.targetPos.x, impact.targetBottom, impact.targetPos.z};
            plan.floor.y += m_floorLift;
            m_plan = plan;
            m_aim.contact = plan.contact;
            m_aim.sparkDir = plan.launchDir;
            if (plan.shape.sparkHeading == SparkHeading::Scrape)
            {
                m_aim.sparkDir = plan.scrapeDir;
            }
            m_aim.recoilDir = plan.selfDir;
            m_aim.ringNormal = plan.ringNormal;

            const ImpactShape& shape = m_plan.shape;
            NS::Gfx::EffectPlayDesc core = PlayAt(m_plan.contact, Quaternion::Identity, Uniform(1.0f));
            core.dynamicInputs[0] = 0.0f;
            core.dynamicInputs[1] = 0.0f;
            core.dynamicInputs[2] = 0.0f;
            core.dynamicInputs[shape.coreInput] = 1.0f;
            core.dynamicInputs[3] = shape.power;
            m_plan.core = NS::PlayEffect(owner.effect, m_coreAsset, core);
            NS::SetEffectAmount(owner.effect, m_plan.core, shape.power);

            // 照りも命中演出の最初から出す
            if (shape.groundLight)
            {
                // 板の法線 (+Z) を上へ向け、床に寝かせる
                const Quaternion lieFlat = TurnNormalTo(Vector3{0.0f, 1.0f, 0.0f});
                NS::Gfx::EffectPlayDesc glow = PlayAt(m_plan.floor, lieFlat, Uniform(1.0f));
                glow.dynamicInputs[0] = static_cast<float>(shape.holdLastFrame);
                glow.dynamicInputs[1] = shape.power;
                m_plan.glow = NS::PlayEffect(owner.effect, m_glowAsset, glow);
                NS::SetEffectAmount(owner.effect, m_plan.glow, shape.power);
            }
        }

        void ImpactEffects::PlaySparks(const Owner& owner)
        {
            const ImpactShape& shape = m_plan.shape;
            Quaternion heading = TurnUpTo(m_plan.launchDir);
            if (shape.sparkHeading == SparkHeading::Scrape)
            {
                heading = TurnUpTo(m_plan.scrapeDir);
            }
            NS::Gfx::EffectPlayDesc sparks = PlayAt(m_plan.contact, heading, Uniform(1.0f));
            sparks.dynamicInputs[0] = shape.power;
            sparks.dynamicInputs[1] = shape.launchScale;
            sparks.dynamicInputs[2] = 0.0f;
            if (shape.sparkHeading == SparkHeading::Scrape)
            {
                sparks.dynamicInputs[2] = 1.0f;
            }
            const NS::uint32 id = NS::PlayEffect(owner.effect, m_sparksAsset, sparks);
            NS::SetEffectAmount(owner.effect, id, shape.power);
            NS::SetEffectRotation(owner.effect, id, heading);
        }

        void ImpactEffects::PlayEmbers(const Owner& owner)
        {
            const ImpactShape& shape = m_plan.shape;
            // 素材の放射軸 (+Y) を相手の飛ぶ向きへ合わせる
            NS::Gfx::EffectPlayDesc embers = PlayAt(m_plan.contact, TurnUpTo(m_plan.launchDir), Uniform(1.0f));
            embers.dynamicInputs[0] = shape.power;
            embers.dynamicInputs[1] = shape.launchScale;
            NS::SetEffectAmount(owner.effect,
                                NS::PlayEffect(owner.effect, m_embersAsset, embers),
                                shape.power);
        }

        void ImpactEffects::AdvanceHitCore(const Owner& owner, int frame)
        {
            if (m_plan.core == 0)
            {
                return;
            }
            const ImpactShape& shape = m_plan.shape;
            if (shape.coreCut)
            {
                if (frame > shape.holdLastFrame)
                {
                    NS::StopEffect(owner.effect, m_plan.core);
                    m_plan.core = 0;
                }
            }
            else if (frame >= shape.holdLastFrame)
            {
                NS::StopEffectRoot(owner.effect, m_plan.core);
                m_plan.core = 0;
            }
        }

        void ImpactEffects::AdvanceHitStreak(const Owner& owner, int frame)
        {
            const ImpactShape& shape = m_plan.shape;
            if (shape.centerLayers && frame == 1)
            {
                NS::Gfx::EffectPlayDesc streak =
                    PlayAt(m_plan.contact, Quaternion::Identity, Uniform(1.0f));
                streak.dynamicInputs[0] = static_cast<float>(shape.holdLastFrame);
                streak.dynamicInputs[1] = shape.power;
                m_plan.streak = NS::PlayEffect(owner.effect, m_streakAsset, streak);
                NS::SetEffectAmount(owner.effect, m_plan.streak, shape.power);
            }
            if (m_plan.streak == 0)
            {
                return;
            }
            const int fadeStart = shape.holdLastFrame + std::max(m_streakTailFrames, 0);
            if (frame >= fadeStart + std::max(m_streakFadeFrames, 0))
            {
                NS::StopEffect(owner.effect, m_plan.streak);
                m_plan.streak = 0;
            }
            else if (frame >= fadeStart)
            {
                // 親を止めた後は太さを置き直さず、絵の定義の落ちるフレーム数で薄れる
                NS::StopEffectRoot(owner.effect, m_plan.streak);
            }
        }

        void ImpactEffects::AdvanceHitGlow(const Owner& owner, int frame)
        {
            if (m_plan.glow == 0)
            {
                return;
            }
            const ImpactShape& shape = m_plan.shape;
            if (frame >= shape.holdLastFrame)
            {
                NS::StopEffectRoot(owner.effect, m_plan.glow);
                m_plan.glow = 0;
            }
        }

        void ImpactEffects::AdvanceHitRing(const Owner& owner, int frame)
        {
            const ImpactShape& shape = m_plan.shape;
            if (shape.centerLayers && frame == std::max(m_ringStart, 0))
            {
                NS::Gfx::EffectPlayDesc ring =
                    PlayAt(m_plan.contact, TurnNormalTo(m_plan.ringNormal), Uniform(1.0f));
                ring.dynamicInputs[0] = shape.power;
                m_plan.ring = NS::PlayEffect(owner.effect, m_ringAsset, ring);
                NS::SetEffectAmount(owner.effect, m_plan.ring, shape.power);
            }
            if (m_plan.ring == 0)
            {
                return;
            }
            if (frame >= m_ringEnd)
            {
                NS::StopEffect(owner.effect, m_plan.ring);
                m_plan.ring = 0;
            }
        }

        void ImpactEffects::AdvanceHitRelease(const Owner& owner, const NS::Collider* collider, int step)
        {
            // 自機の側は飛びの絵の頭から。当たりの絵の頭で相手の側、飛びの絵の頭で自機の側が出て、作用と反作用の順に読める
            if (m_plan.releaseStep < 0)
            {
                return;
            }
            const ImpactShape& shape = m_plan.shape;
            if (!m_plan.dustPlayed && step == m_plan.releaseStep)
            {
                m_plan.dustPlayed = true;
                if (shape.groundDust)
                {
                    m_aim.dustOrigin = m_plan.floor;
                    NS::Gfx::EffectPlayDesc dust =
                        PlayAt(m_aim.dustOrigin, Quaternion::Identity, Uniform(1.0f));
                    dust.dynamicInputs[0] = shape.power;
                    dust.dynamicInputs[1] = shape.rootMass;
                    dust.dynamicInputs[2] = m_plan.awayDir.x;
                    dust.dynamicInputs[3] = m_plan.awayDir.z;
                    const NS::uint32 id = NS::PlayEffect(owner.effect, m_dustAsset, dust);
                    NS::SetEffectAmount(owner.effect, id, shape.power);
                    m_dusts.push_back(id);
                }
            }
            // 外れには弾き返されたことを示す線を出さない
            const bool recoilDue = !m_plan.recoilPlayed && step == m_plan.releaseStep + m_recoilDelay;
            if (recoilDue)
            {
                m_plan.recoilPlayed = true;
            }
            if (recoilDue && shape.centerLayers)
            {
                // 接触点から出すと動く方向を誤読しやすいため、反動と逆側の玉の縁から線を出す
                const float radius = collider->CapsuleRadius();
                m_aim.recoilOrigin = owner.position - m_plan.selfDir * radius;
                NS::Gfx::EffectPlayDesc recoil = PlayAt(m_aim.recoilOrigin, TurnUpTo(m_plan.selfDir), Uniform(1.0f));
                recoil.dynamicInputs[0] = shape.reboundScale;
                NS::SetEffectAmount(
                    owner.effect, NS::PlayEffect(owner.effect, m_recoilAsset, recoil), shape.reboundScale);
            }
        }

        void ImpactEffects::AdvanceHit(const Owner& owner, const NS::Collider* collider)
        {
            if (!m_plan.active)
            {
                return;
            }
            const int step = NS::GetEffectStep(owner.effect);
            const int frame = step - m_plan.freezeStep;
            const ImpactShape& shape = m_plan.shape;

            if (!m_plan.sparksPlayed && frame == shape.sparkStartFrame)
            {
                m_plan.sparksPlayed = true;
                PlaySparks(owner);
            }

            if (!m_plan.embersPlayed && shape.centerLayers && frame == shape.emberStartFrame)
            {
                m_plan.embersPlayed = true;
                PlayEmbers(owner);
            }

            AdvanceHitCore(owner, frame);
            AdvanceHitStreak(owner, frame);
            AdvanceHitGlow(owner, frame);
            AdvanceHitRing(owner, frame);
            AdvanceHitRelease(owner, collider, step);

            const bool held = m_plan.core != 0 || m_plan.streak != 0 || m_plan.ring != 0 || m_plan.glow != 0;
            const bool embersDone = m_plan.embersPlayed || !shape.centerLayers;
            if (!held && embersDone && m_plan.recoilPlayed && m_plan.dustPlayed)
            {
                m_plan.active = false;
            }
        }

        void ImpactEffects::BeginFlight(const Owner& owner,
                                        const PlayerMotion& motion,
                                        const NS::Body* body,
                                        const GL::Level::ImpactRecord& impact)
        {
            const int step = NS::GetEffectStep(owner.effect);
            // 前の当たりの尾が残っていれば、ここで消す。尾は 1 本ずつ持つ
            if (m_flight.reboundTrail != 0)
            {
                NS::StopEffect(owner.effect, m_flight.reboundTrail);
                m_flight.reboundTrail = 0;
            }
            // 貫通した時と反動の初速が 0 の時は反動に入らないので、尾は出さない
            // 外れは弾かれた手応えを出さない。尾の輪と筋は弾き返された絵に見える
            const bool missed = impact.tier == GL::Level::HitTier::Wide;
            if (motion.rebounding && !missed)
            {
                const Vector3 ball = owner.position;
                const Vector3 heading = ReboundTrailHeading(body->Velocity(), ball, CameraPosition(owner));
                m_flight.reboundTrail = NS::PlayEffect(
                    owner.effect, m_reboundTrailAsset, PlayAt(ball, TurnUpTo(heading), Uniform(1.0f)));
                m_flight.reboundStartStep = step;
            }
            // 飛ばした相手の飛び出しの尾は、相手が自分で出す (LaunchEffects)
        }

        void ImpactEffects::AdvanceFlight(const Owner& owner, const PlayerMotion& motion, const NS::Body* body)
        {
            const int step = NS::GetEffectStep(owner.effect);
            if (m_flight.reboundTrail != 0 && step != m_flight.reboundStartStep)
            {
                // 頂点の前に反動を抜けた時 (縁を掴んだ時など) は、そこで輪ごと消す
                if (!motion.rebounding || motion.reboundLanded)
                {
                    NS::StopEffect(owner.effect, m_flight.reboundTrail);
                    m_flight.reboundTrail = 0;
                }
                else if (!(body->VerticalVelocity() > 0.0f))
                {
                    // 頂点は縦の速さが 0 以下になったフレーム。親を止め、筋は頂点の位置と向きのまま薄れる
                    // 親を止めた後の筋は置き直しても動かない (Effekseer は親の最後の姿で子を描く)
                    NS::StopEffectRoot(owner.effect, m_flight.reboundTrail);
                    NS::StopEffectAfter(
                        owner.effect, m_flight.reboundTrail, std::max(m_reboundTrailFadeSteps, 1));
                    m_flight.reboundTrail = 0;
                }
                else
                {
                    const Vector3 ball = owner.position;
                    const Vector3 heading = ReboundTrailHeading(body->Velocity(), ball, CameraPosition(owner));
                    NS::SetEffectTransform(owner.effect, m_flight.reboundTrail, ball, TurnUpTo(heading), Uniform(1.0f));
                }
            }
        }

        void ImpactEffects::AdvanceLanding(const Owner& owner,
                                           const PlayerMotion& motion,
                                           const NS::Body* body,
                                           const NS::Collider* collider)
        {
            if (!motion.rebounding)
            {
                m_landingDustPlayed = false;
            }
            else if (!m_landingDustPlayed && motion.reboundLanded)
            {
                // このフレームの縦の速さは既に 0 なので、前のフレームの控えで測る
                m_landingDustPlayed = true;
                const float fallSpeed = std::max(-m_lastVerticalVelocity, 0.0f);
                // 足元は当たりのカプセルの下端 (中心 − 軸 × (半分の高さ + 半径))
                const NS::Phys::Capsule capsule = collider->CapsuleAt(owner.position);
                Vector3 at = capsule.center - capsule.axis * (capsule.halfHeight + capsule.radius);
                at.y += m_dustRingLift;
                NS::Gfx::EffectPlayDesc desc = PlayAt(at, Quaternion::Identity, Uniform(1.0f));
                desc.dynamicInputs[0] = fallSpeed;
                const NS::uint32 dust = NS::PlayEffect(owner.effect, m_landDustAsset, desc);
                NS::SetEffectAmount(owner.effect, dust, fallSpeed);
                NS::StopEffectAfter(owner.effect, dust, std::max(m_landDustLife, 1));
            }
            m_lastVerticalVelocity = body->VerticalVelocity();
        }

        std::optional<Vector3> ImpactEffects::CameraPosition(const Owner& owner)
        {
            // 揺れを掛ける前の位置。輪と尾は世界に置く物なので、揺れの間に向きが震えない
            const std::optional<NS::CameraPose> pose = NS::GetCameraViewPose(owner.camera);
            if (!pose.has_value())
            {
                return std::nullopt;
            }
            return pose->position;
        }

        void ImpactEffects::FinishHeldEffects(const Owner& owner)
        {
            if (!m_plan.active)
            {
                return;
            }
            NS::StopEffectRoot(owner.effect, m_plan.core);
            NS::StopEffectRoot(owner.effect, m_plan.glow);
            NS::StopEffect(owner.effect, m_plan.streak);
            NS::StopEffect(owner.effect, m_plan.ring);
            m_plan = HitPlan{};
        }
    } // namespace Player
} // namespace GL
