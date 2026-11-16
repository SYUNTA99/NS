#include "Game/Player/ImpactEffects.h"

#include "Game/Player/PlayerStatus.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/CoreObject/IGet/IGetCollision.h"
#include "NSlib/CoreObject/IGet/IGetEffect.h"

#include <algorithm>
#include <cmath>

namespace GL
{
    namespace Player
    {
        namespace
        {
            using NS::Quaternion;
            using NS::TurnNormalTo;
            using NS::TurnUpTo;
            using NS::Vector3;

            // 素材の前 (+Z) を水平の向きへ回す。向きが決まらない時は回さない
            [[nodiscard]] Quaternion YawTo(const Vector3& horizontal) noexcept
            {
                if (!NS::HasPositiveMagnitudeHorizontal(horizontal))
                {
                    return Quaternion::Identity;
                }
                return Quaternion::CreateFromAxisAngle(Vector3{0.0f, 1.0f, 0.0f},
                                                       std::atan2(horizontal.x, horizontal.z));
            }
        } // namespace

        void ImpactEffects::Reset(const NS::IGetEffect* user)
        {
            NS_ASSERT(Game, user != nullptr, "エフェクトを出したアクターが空");
            // 番号は StopEffect が一覧から外すので、先に控える
            std::vector<NS::uint32> playing;
            if (const NS::EffectObj* effects = user->GetEffect())
            {
                for (const NS::PlayingEffect& record : effects->Records())
                {
                    playing.push_back(record.id);
                }
            }
            for (const NS::uint32 id : playing)
            {
                NS::StopEffect(user, id);
            }
            m_plan = HitPlan{};
            m_aim = ImpactAim{};
            m_hitRequested = false;
            m_flightRequested = false;
            m_timedStops.clear();
            m_dusts.clear();
            m_reboundTrail = 0;
            m_dashTrail = 0;
            m_wasDashing = false;
            m_dashDirection = Vector3{};
            m_landingArmed = false;
            m_awaitingLanding = false;
            m_lastVerticalVelocity = 0.0f;
        }

        void ImpactEffects::RequestHit(const TerrainHit& hit) noexcept
        {
            m_requestedHit = hit;
            m_hitRequested = true;
        }

        void ImpactEffects::Update(const NS::IGetEffect* effect,
                                   const NS::IGetCamera* camera,
                                   const NS::IGetCollision* collision,
                                   const PlayerMotion& motion,
                                   const ImpactSelf& self)
        {
            NS_ASSERT(Game, effect != nullptr && camera != nullptr, "エフェクトを出すアクターかカメラを引くアクターが空");
            NS_ASSERT(Game, collision != nullptr, "床を調べるアクターが空");
            const Owner owner{.effect = effect, .camera = camera, .collision = collision, .self = self};
            StopDueEffects(owner);
            AdvanceDash(owner, motion);
            if (m_hitRequested)
            {
                m_hitRequested = false;
                BeginHit(owner);
            }
            if (m_flightRequested)
            {
                m_flightRequested = false;
                if (m_plan.active)
                {
                    m_plan.releaseFrame = m_frame;
                    BeginFlight(owner, motion);
                }
            }
            AdvanceHit(owner);
            AdvanceFlight(owner, motion);
            AdvanceLanding(owner, motion);
            m_frame = m_frame + 1;
        }

        NS::uint32 ImpactEffects::Play(const Owner& owner,
                                       const NS::Effect* asset,
                                       const Vector3& position,
                                       const Quaternion& rotation,
                                       std::initializer_list<float> inputs) const
        {
            const NS::uint32 id = NS::PlayEffect(owner.effect, asset, position);
            if (id == 0)
            {
                return 0;
            }
            NS::SetEffectTransform(owner.effect, id, position, rotation, NS::UniformScale(1.0f));
            int index = 0;
            for (const float value : inputs)
            {
                NS::SetEffectDynamicInput(owner.effect, id, index, value);
                index = index + 1;
            }
            return id;
        }

        void ImpactEffects::FadeOut(const Owner& owner, NS::uint32 id, int frames)
        {
            if (id == 0)
            {
                return;
            }
            // 親を止めた後の子は置き直しても動かない (Effekseer は親の最後の姿で子を描く)
            NS::StopEffectRoot(owner.effect, id);
            m_timedStops.push_back(TimedStop{.id = id, .frame = m_frame + std::max(frames, 1)});
        }

        void ImpactEffects::StopDueEffects(const Owner& owner)
        {
            for (const TimedStop& stop : m_timedStops)
            {
                if (m_frame >= stop.frame)
                {
                    NS::StopEffect(owner.effect, stop.id);
                }
            }
            std::erase_if(m_timedStops, [this](const TimedStop& stop) { return m_frame >= stop.frame; });
        }

        bool ImpactEffects::FindFloor(const Owner& owner, Vector3& outFloor) const
        {
            const Vector3 down{0.0f, -1.0f, 0.0f};
            float distance = 0.0f;
            if (!NS::RaycastCollision(owner.collision, owner.self.position, down, m_floorProbeDrop, distance))
            {
                return false;
            }
            outFloor = owner.self.position + down * distance;
            outFloor.y += m_floorLift;
            return true;
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
            if (!NS::HasPositiveMagnitude(sight))
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

        void ImpactEffects::AimHit(const Owner& owner, HitPlan& plan) const
        {
            const TerrainHit& hit = plan.hit;
            const Vector3 normal = NS::NormalizedOr(hit.normal, Vector3{0.0f, 1.0f, 0.0f});
            plan.sparkDir = normal;
            plan.holdLastFrame = std::max(m_wideCoreHoldLast, 0);
            plan.sparkStartFrame = 0;
            if (hit.center)
            {
                // 核の留まりは止めと結ぶ。止めの短い当たりは明けの前に落ち始め、明けの後に光が残らない
                plan.holdLastFrame = std::max(1, std::min(m_coreHoldMax, owner.self.stopFrames - 1));
                plan.sparkStartFrame = std::max(m_centerSparkStart, 0);
            }
            else
            {
                // 外れの段は、面に沿って滑った向きへ擦れた火花を出す。真っ直ぐ当てた時は滑りが無いので法線と上だけ
                const Vector3 slide = NS::NormalizedOr(NS::RejectUnit(hit.incoming, normal), Vector3{});
                plan.sparkDir = NS::NormalizedOr(
                    slide + normal * m_scrapeLaunchWeight + Vector3{0.0f, m_scrapeLiftWeight, 0.0f}, normal);
            }
            // 輪の法線は面の法線を残しつつカメラへ起こす。真横から見て縦の線に潰れない
            Vector3 toCamera{};
            const std::optional<Vector3> camera = CameraPosition(owner);
            if (camera.has_value())
            {
                toCamera = NS::NormalizedOr(camera.value() - hit.contact, Vector3{});
            }
            plan.ringNormal = NS::NormalizedOr(normal * m_ringLaunchWeight + toCamera * m_ringFaceCamera, normal);
            plan.flightDir = NS::NormalizedOr(hit.rebound, normal);
            plan.hasFloor = FindFloor(owner, plan.floor);
        }

        void ImpactEffects::BeginHit(const Owner& owner)
        {
            // 前の当たりの層が残っていても、次の核は当たったフレームから出す。前の粉は親を止めて短くする
            FinishHeldEffects(owner);
            // 反動の途中で次に当てたら、その反動はここで終わる。尾は輪ごと消す
            NS::StopEffect(owner.effect, m_reboundTrail);
            m_reboundTrail = 0;
            for (const NS::uint32 dust : m_dusts)
            {
                NS::StopEffectRoot(owner.effect, dust);
            }
            m_dusts.clear();

            HitPlan plan;
            plan.active = true;
            plan.hit = m_requestedHit;
            plan.startFrame = m_frame;
            AimHit(owner, plan);
            m_plan = plan;
            m_landingArmed = true;
            m_awaitingLanding = false;
            m_aim = ImpactAim{.contact = plan.hit.contact,
                              .sparkDir = plan.sparkDir,
                              .recoilDir = plan.flightDir,
                              .ringNormal = plan.ringNormal};

            // 核の入力 0〜2 は段の色の節を選び、入力 3 は強さ。外れの段は入力 2
            const float power = plan.hit.power;
            if (plan.hit.center)
            {
                m_plan.core = Play(owner, m_coreAsset, plan.hit.contact, Quaternion::Identity, {1.0f, 0.0f, 0.0f, power});
            }
            else
            {
                m_plan.core = Play(owner, m_coreAsset, plan.hit.contact, Quaternion::Identity, {0.0f, 0.0f, 1.0f, power});
            }
            // 照りも当たったフレームから出す。板の法線 (+Z) を上へ向け、床に寝かせる
            if (plan.hit.center && plan.hasFloor)
            {
                m_plan.glow = Play(owner,
                                   m_glowAsset,
                                   plan.floor,
                                   TurnNormalTo(Vector3{0.0f, 1.0f, 0.0f}),
                                   {static_cast<float>(plan.holdLastFrame), power});
            }
        }

        void ImpactEffects::AdvanceHitCore(const Owner& owner, int frame)
        {
            if (m_plan.glow != 0 && frame >= m_plan.holdLastFrame)
            {
                NS::StopEffectRoot(owner.effect, m_plan.glow);
                m_plan.glow = 0;
            }
            if (m_plan.core == 0)
            {
                return;
            }
            if (!m_plan.hit.center)
            {
                // 外れの核は留まりの次のフレームで消す
                if (frame > m_plan.holdLastFrame)
                {
                    NS::StopEffect(owner.effect, m_plan.core);
                    m_plan.core = 0;
                }
                return;
            }
            // 中心の核は留まりの最後のフレームに親を止め、素材の定義の落ちるフレーム数で薄れる
            if (frame >= m_plan.holdLastFrame)
            {
                NS::StopEffectRoot(owner.effect, m_plan.core);
                m_plan.core = 0;
            }
        }

        void ImpactEffects::AdvanceHitStreak(const Owner& owner, int frame)
        {
            if (m_plan.hit.center && frame == 1)
            {
                m_plan.streak = Play(owner,
                                     m_streakAsset,
                                     m_plan.hit.contact,
                                     Quaternion::Identity,
                                     {static_cast<float>(m_plan.holdLastFrame), m_plan.hit.power});
            }
            if (m_plan.streak == 0)
            {
                return;
            }
            const int fadeStart = m_plan.holdLastFrame + std::max(m_streakTailFrames, 0);
            if (frame >= fadeStart + std::max(m_streakFadeFrames, 0))
            {
                NS::StopEffect(owner.effect, m_plan.streak);
                m_plan.streak = 0;
            }
            else if (frame >= fadeStart && !m_plan.streakFading)
            {
                m_plan.streakFading = true;
                NS::StopEffectRoot(owner.effect, m_plan.streak);
            }
        }

        void ImpactEffects::AdvanceHitRing(const Owner& owner, int frame)
        {
            if (m_plan.hit.center && frame == std::max(m_ringStart, 0))
            {
                m_plan.ring =
                    Play(owner, m_ringAsset, m_plan.hit.contact, TurnNormalTo(m_plan.ringNormal), {m_plan.hit.power});
            }
            if (m_plan.ring != 0 && frame >= m_ringEnd)
            {
                NS::StopEffect(owner.effect, m_plan.ring);
                m_plan.ring = 0;
            }
        }

        void ImpactEffects::AdvanceHitRelease(const Owner& owner)
        {
            // 自機の側は飛び出すフレームから。当たったフレームに面の側、飛び出すフレームに自機の側が出て、
            // 作用と反作用の順に読める
            if (m_plan.releaseFrame < 0)
            {
                return;
            }
            const float power = m_plan.hit.power;
            if (!m_plan.dustPlayed && m_frame == m_plan.releaseFrame)
            {
                m_plan.dustPlayed = true;
                if (m_plan.hasFloor)
                {
                    m_aim.dustOrigin = m_plan.floor;
                    // 入力 2 と 3 は、粉を自機から離す水平の向き (当たった面の側)。入力 1 は相手の重さの平方根で、地形は 1
                    const Vector3 away =
                        NS::NormalizedOr(Vector3{-m_plan.hit.normal.x, 0.0f, -m_plan.hit.normal.z}, Vector3{});
                    m_dusts.push_back(
                        Play(owner, m_dustAsset, m_plan.floor, Quaternion::Identity, {power, 1.0f, away.x, away.z}));
                }
            }
            const bool recoilDue = !m_plan.recoilPlayed && m_frame >= m_plan.releaseFrame + m_recoilDelay;
            if (!recoilDue)
            {
                return;
            }
            m_plan.recoilPlayed = true;
            // 外れの段には、弾き返された事を示す線を出さない
            if (m_plan.hit.center)
            {
                // 衝突点から出すと動く向きを読み違えやすいので、飛ぶ向きの逆の玉の表面から線を出す
                m_aim.recoilOrigin = owner.self.position - m_plan.flightDir * owner.self.radius;
                (void)Play(owner, m_recoilAsset, m_aim.recoilOrigin, TurnUpTo(m_plan.flightDir), {power});
            }
        }

        void ImpactEffects::AdvanceHit(const Owner& owner)
        {
            if (!m_plan.active)
            {
                return;
            }
            const int frame = m_frame - m_plan.startFrame;
            const float power = m_plan.hit.power;
            if (!m_plan.sparksPlayed && frame >= m_plan.sparkStartFrame)
            {
                m_plan.sparksPlayed = true;
                // 入力 2 は、擦れた火花の形にするか
                float scrape = 1.0f;
                if (m_plan.hit.center)
                {
                    scrape = 0.0f;
                }
                m_plan.sparks =
                    Play(owner, m_sparksAsset, m_plan.hit.contact, TurnUpTo(m_plan.sparkDir), {power, power, scrape});
            }
            // 火の粉は核が落ちるフレームに、衝突点から新しく出す
            if (!m_plan.embersPlayed && m_plan.hit.center && frame >= m_plan.holdLastFrame + 1)
            {
                m_plan.embersPlayed = true;
                (void)Play(owner, m_embersAsset, m_plan.hit.contact, TurnUpTo(m_plan.sparkDir), {power, power});
            }
            AdvanceHitCore(owner, frame);
            AdvanceHitStreak(owner, frame);
            AdvanceHitRing(owner, frame);
            AdvanceHitRelease(owner);

            const bool held = m_plan.core != 0 || m_plan.streak != 0 || m_plan.ring != 0 || m_plan.glow != 0;
            const bool embersDone = m_plan.embersPlayed || !m_plan.hit.center;
            if (!held && embersDone && m_plan.recoilPlayed && m_plan.dustPlayed)
            {
                m_plan.active = false;
            }
        }

        void ImpactEffects::BeginFlight(const Owner& owner, const PlayerMotion& motion)
        {
            m_plan.flightDir = NS::NormalizedOr(motion.velocity, m_plan.flightDir);
            m_aim.recoilDir = m_plan.flightDir;
            NS::StopEffect(owner.effect, m_reboundTrail);
            m_reboundTrail = 0;
            // 外れの段は弾かれた手応えを出さない。尾の輪と筋は弾き返された絵に見える
            if (!motion.dashRebounding || !m_plan.hit.center)
            {
                return;
            }
            const Vector3 heading = ReboundTrailHeading(motion.velocity, owner.self.position, CameraPosition(owner));
            m_reboundTrail = Play(owner, m_reboundTrailAsset, owner.self.position, TurnUpTo(heading), {});
            m_reboundTrailStartFrame = m_frame;
        }

        void ImpactEffects::AdvanceFlight(const Owner& owner, const PlayerMotion& motion)
        {
            if (m_reboundTrail == 0 || m_frame == m_reboundTrailStartFrame)
            {
                return;
            }
            if (!motion.dashRebounding || owner.self.grounded)
            {
                FadeOut(owner, m_reboundTrail, m_reboundTrailFadeFrames);
                m_reboundTrail = 0;
                return;
            }
            const Vector3 heading = ReboundTrailHeading(motion.velocity, owner.self.position, CameraPosition(owner));
            NS::SetEffectTransform(
                owner.effect, m_reboundTrail, owner.self.position, TurnUpTo(heading), NS::UniformScale(1.0f));
        }

        void ImpactEffects::AdvanceDash(const Owner& owner, const PlayerMotion& motion)
        {
            if (motion.airDashing && NS::HasPositiveMagnitudeHorizontal(motion.velocity))
            {
                m_dashDirection = Vector3{motion.velocity.x, 0.0f, motion.velocity.z};
            }
            if (motion.airDashing && !m_wasDashing)
            {
                FadeOut(owner, m_dashTrail, m_dashTrailFadeFrames);
                // 入力 0 は尾の濃さ。溜めの無いエアダッシュは一番濃い 1
                m_dashTrail = Play(owner, m_dashTrailAsset, owner.self.position, YawTo(m_dashDirection), {1.0f});
            }
            else if (m_dashTrail != 0 && !motion.airDashing)
            {
                // 面に当たったフレームと、当たらずに終わったフレームに薄れ始める
                FadeOut(owner, m_dashTrail, m_dashTrailFadeFrames);
                m_dashTrail = 0;
            }
            else if (m_dashTrail != 0)
            {
                NS::SetEffectTransform(
                    owner.effect, m_dashTrail, owner.self.position, YawTo(m_dashDirection), NS::UniformScale(1.0f));
            }
            m_wasDashing = motion.airDashing;
        }

        void ImpactEffects::AdvanceLanding(const Owner& owner, const PlayerMotion& motion)
        {
            if (!owner.self.grounded)
            {
                if (m_landingArmed)
                {
                    m_landingArmed = false;
                    m_awaitingLanding = true;
                }
            }
            else if (m_awaitingLanding)
            {
                m_awaitingLanding = false;
                // このフレームの縦の速さは既に 0 なので、前のフレームの控えで測る
                const float fallSpeed = std::max(-m_lastVerticalVelocity, 0.0f);
                Vector3 at = owner.self.foot;
                at.y += m_dustRingLift;
                const NS::uint32 dust = Play(owner, m_landDustAsset, at, Quaternion::Identity, {fallSpeed});
                if (dust != 0)
                {
                    m_timedStops.push_back(TimedStop{.id = dust, .frame = m_frame + std::max(m_landDustLife, 1)});
                }
            }
            else if (m_landingArmed && owner.self.stopFrames <= 0 && motion.velocity.y <= 0.0f)
            {
                // 当たった後に宙へ出ないまま止めが明けた。後の、当たりと関係の無い着地で粉を出さない
                m_landingArmed = false;
            }
            m_lastVerticalVelocity = motion.velocity.y;
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
            // 止めの間の押しで束を始め直した時に、外れの段の擦れた火花が中心の段の火花と重ならない
            NS::StopEffect(owner.effect, m_plan.sparks);
            m_plan = HitPlan{};
        }
    } // namespace Player
} // namespace GL
