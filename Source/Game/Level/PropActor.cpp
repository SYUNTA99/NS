#include "Game/Level/PropActor.h"
#include "NSlib/Core/Types.h"

#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/EffectKeeper.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Windows/Clock.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <vector>

namespace GL
{
    namespace Level
    {
        class PropActor::RestingState : public NS::StateOf<RestingState, PropActor>
        {
            void OnEnter(PropActor& owner) override
            {
                owner.m_velocity = NS::Vector3{};
                owner.m_restAge = 0.0f;
            }
            void OnStep(PropActor& owner, float dt) override
            {
                if (owner.m_hasLaunched && owner.m_params->RestLifeSeconds() > 0.0f)
                {
                    owner.m_restAge += dt;
                    if (owner.m_restAge >= owner.m_params->RestLifeSeconds())
                    {
                        owner.BodySensorClassObj()->Invalidate();
                        owner.Sphere().RemoveFromPhysics();
                        owner.Disappear();
                    }
                }
            }
        };

        // 受けた状態。止めの間は基底が状態の段を飛ばすので、OnStep が回るのは自分の束の止めが明けた後
        class PropActor::FreezeState : public NS::StateOf<FreezeState, PropActor>
        {
            void OnStep(PropActor& owner, float dt) override
            {
                if (!owner.m_freezePlaced)
                {
                    owner.m_motion.Step(dt);
                }
                owner.LeaveFreeze();
            }
        };

        class PropActor::LaunchedState : public NS::StateOf<LaunchedState, PropActor>
        {
            void OnStep(PropActor& owner, float dt) override { owner.StepLaunched(dt); }
        };

        class PropActor::ArcState : public NS::StateOf<ArcState, PropActor>
        {
            void OnStep(PropActor& owner, float dt) override { owner.StepArc(dt); }
        };

        class PropActor::RollingState : public NS::StateOf<RollingState, PropActor>
        {
            void OnStep(PropActor& owner, float dt) override { owner.StepRolling(dt); }
        };

        PropActor::PropActor() noexcept
        {
            m_motion.Finish();
        }
        void PropActor::OnInit()
        {
            CreateClassObj(m_model, this);
            CreateClassObj(m_shadow, this);
            NS::SphereCollision* sphere = CreateClassObj<NS::Collision, NS::SphereCollision>(m_collision, this);
            CreateClassObj(m_params);
            CreateClassObj(m_hitZones);
            CreateClassObj(m_effectKeeper, this);
            CreateClassObj(m_hitReaction, this);
            CreateClassObj(m_effects, this);
            CreateClassObj(m_marks, this);
            // 体当たりが調べる体は当たりの球そのもの。半径と中心オフセットの正は Collision の欄
            NS::FollowHitSensor* bodySensor =
                CreateClassObj<NS::HitSensor, NS::FollowHitSensor>(m_bodySensor, this, [sphere] {
                    const NS::Sphere world = sphere->WorldSphere();
                    return NS::SensorVolume::Sphere(world.center, world.radius);
                });
            SetSensorKind(*bodySensor, SensorKind::MapObjBody);
            (void)BuildStateMachine<PropActor, RestingState, FreezeState, LaunchedState>(*this, m_states);
        }

        void PropActor::OnStart()
        {
            NS::SceneActor::OnStart();
            m_effects->Preload();
        }

        void PropActor::OnAppear()
        {
            NS::SceneActor::OnAppear();
            if (IsAppeared())
            {
                m_tackleGhost.Register();
                m_marks->Register();
            }
        }

        void PropActor::OnDisappear() noexcept
        {
            NS::SceneActor::OnDisappear();
            m_tackleGhost.Unregister();
            m_marks->Unregister();
        }

        bool PropActor::IsFrozen() const noexcept
        {
            return m_states->IsCurrent<FreezeState>();
        }

        bool PropActor::IsFlying() const noexcept
        {
            return m_states->IsCurrent<LaunchedState>() || (IsFrozen() && !m_freezePlaced && !m_motion.IsDead());
        }

        bool PropActor::IsArc() const noexcept
        {
            return IsFlying() && m_motion.Machine().IsCurrent<ArcState>();
        }

        PropFlight PropActor::Flight() const noexcept
        {
            PropFlight flight;
            flight.position = Root().Position();
            flight.velocity = m_velocity;
            flight.arc = IsArc();
            return flight;
        }

        void PropActor::VisualStep()
        {
            // clang-format off
            m_effects->Update(Flight());
            // clang-format on
            // 明けの写しは止めの後も残り、写し取る姿は揺れを書いた後の姿なので、Model が揺れを進めた後で進める
            m_ghost.Advance(&m_tackleGhost);
            m_marks->Step(NS::OS::FrameTimer::FixedDelta());
        }

        void PropActor::UpdateMotion()
        {
            Update();
        }

        void PropActor::BeginFreeze(const TackleFreezeDesc& desc)
        {
            NS_ASSERT(Game, !IsFrozen(), "受けた状態のまま次の受けを始めた。前の受けは ReceiveHit が解く");
            m_freezePlaced = !IsFlying();
            m_freezeHome = Root().Position();
            m_freeze = desc;
            m_freeze.squash = false;
            (void)m_states->Change<FreezeState>();
            if (m_freezePlaced)
            {
                Root().SetPosition(m_freezeHome + desc.impactDir * desc.pushInDistance);
                if (desc.squash)
                {
                    const float x2 = desc.impactDir.x * desc.impactDir.x;
                    const float z2 = desc.impactDir.z * desc.impactDir.z;
                    const NS::Vector3 squash{1.0f + (desc.squashThickness - 1.0f) * x2,
                                             desc.squashHeight,
                                             1.0f + (desc.squashThickness - 1.0f) * z2};
                    // 有限の正でない欄の値では潰さない
                    m_freeze.squash = NS::IsPositiveFinite(squash.x) && NS::IsPositiveFinite(squash.y) &&
                                      NS::IsPositiveFinite(squash.z);
                    if (m_freeze.squash)
                    {
                        (void)ModelClassObj()->SnapDrawScale(squash);
                    }
                }
            }
            // 次の歩を待たず、同じ歩の後の問いに置き直した当たりを見せる
            Sphere().SyncToPhysics();
        }

        void PropActor::ReceiveHit(const TackleHitDesc& hit, const NS::HitSensor& self, const NS::HitSensor& attacker)
        {
            // 前の受けを先に解く。解くと揺れが止まるので、新しい束の揺れより前に置く
            if (IsFrozen())
            {
                LeaveFreeze();
            }
            m_received = ReceivedHit{
                .attacker = NS::MakeHitSensorRef(attacker), .contact = hit.contact, .tier = hit.tier, .active = true};
            // 束が足した持ち主へ渡す項目は、走っている項目の後ろに並ぶ
            const std::size_t first = m_hitReaction->OwnerRuns().size();
            (void)m_hitReaction->Start(TackleSetName("受け", hit.tier), hit.contact, &self, &attacker);
            if (hit.stopOverride.has_value())
            {
                OverrideHitStop(std::max(*hit.stopOverride, 0));
            }
            TackleFreezeDesc freeze{.impactDir = hit.impactDir};
            std::optional<NS::HitOwnerRun> ghost;
            const std::vector<NS::HitOwnerRun>& runs = m_hitReaction->OwnerRuns();
            for (std::size_t i = first; i < runs.size(); ++i)
            {
                const NS::HitOwnerItem& item = runs[i].item;
                if (item.name == "TackleFreeze")
                {
                    freeze.pushInDistance = HitOwnerValue(item, "食い込み距離", 0.0f);
                    freeze.squashThickness = HitOwnerValue(item, "潰れの厚み", 1.0f);
                    freeze.squashHeight = HitOwnerValue(item, "潰れの伸び上がり", 1.0f);
                    // 貫通は押し勝っている側なので潰さない
                    freeze.squash = !hit.stopOverride.has_value();
                }
                if (item.name == "TackleGhost")
                {
                    ghost = runs[i];
                }
            }
            BeginFreeze(freeze);
            // 残像の離れは食い込んだ所の奥行きで出す
            if (ghost.has_value())
            {
                m_ghost.Start(*ghost, this);
            }
        }

        void PropActor::StartReleaseSet()
        {
            if (!m_received.active)
            {
                return;
            }
            m_received.active = false;
            const NS::HitSensor* attacker = NS::FindHitSensor(OwningScene(), m_received.attacker);
            if (attacker == nullptr)
            {
                return;
            }
            (void)m_hitReaction->Start(
                TackleSetName("明け", m_received.tier), m_received.contact, BodySensorClassObj(), attacker);
        }

        void PropActor::EndFreeze()
        {
            ModelClassObj()->StopDrawShake();
            if (m_freezePlaced)
            {
                Root().SetPosition(m_freezeHome);
            }
            if (m_freeze.squash)
            {
                (void)ModelClassObj()->SnapDrawScale(NS::Vector3{1.0f, 1.0f, 1.0f});
                m_freeze.squash = false;
            }
        }

        void PropActor::LeaveFreeze()
        {
            EndFreeze();
            if (m_freezePlaced || m_motion.IsDead())
            {
                (void)m_states->Change<RestingState>();
            }
            else
            {
                (void)m_states->Change<LaunchedState>();
            }
        }

        void PropActor::Release(const TackleReleaseDesc& desc)
        {
            if (IsFrozen())
            {
                LeaveFreeze();
            }
            SpawnMark();
            const NS::Vector3 up = -GravityDirection();
            NS::Vector3 forward = desc.arc.direction - up * NS::Dot(desc.arc.direction, up);
            const float length = forward.Length();
            if (!NS::IsFinite(forward) || !(length > 0.0001f))
            {
                return;
            }
            LaunchArc arc = desc.arc;
            arc.direction = NS::Vector3{1.0f, 0.0f, 0.0f};
            const NS::Vector3 initial = LaunchArcInitialVelocity(arc);
            if (!(initial.Length() > 0.0f))
            {
                return;
            }
            m_arc = arc;
            m_arcUp = up;
            m_arcForward = forward / length;
            m_arcSeconds = 0.0f;
            m_arcDeflected = false;
            m_hopsLeft = 0;
            if (desc.tier == HitTier::Wide)
            {
                m_hopsLeft = m_params->MissHopCount();
            }
            m_hopIndex = 0;
            m_hopSeed = desc.hopSeed;
            m_velocity = m_arcForward * initial.x + m_arcUp * initial.y;
            m_hasLaunched = true;
            m_motion.Build<ArcState, RollingState>(*this);
            (void)m_states->Change<LaunchedState>();
            m_effects->BeginTrail(desc, Flight(), m_params.get(), &Sphere());
            // 次の歩を待たず、同じ歩の後の問いに置き直した当たりを見せる
            Sphere().SyncToPhysics();
        }

        NS::Vector3 PropActor::ArcOffset(float seconds) const noexcept
        {
            const NS::Vector3 offset = LaunchArcOffsetAt(m_arc, seconds);
            return m_arcForward * offset.x + m_arcUp * offset.y;
        }

        void PropActor::StepLaunched(float dt)
        {
            m_motion.Step(dt);
            if (m_motion.IsDead())
            {
                (void)m_states->Change<RestingState>();
            }
        }

        void PropActor::StepArc(float dt)
        {
            const NS::Vector3 before = ArcOffset(m_arcSeconds);
            m_arcSeconds += dt;
            const NS::Vector3 delta = ArcOffset(m_arcSeconds) - before;
            if (!m_arcDeflected)
            {
                m_velocity = delta / dt;
            }
            else
            {
                // 曲がった後も曲線と同じ重力を自機と同じ選び方で積む。AddGravity は下向きを正で受ける
                const float vertical = NS::Dot(m_velocity, m_arcUp);
                AddGravity(m_velocity, -ChooseGravity(LaunchArcGravity(m_arc), vertical), dt);
            }
            MoveLaunched(dt);
            if (!m_motion.Machine().IsCurrent<ArcState>())
            {
                return;
            }
            if (NS::Dot(m_velocity, m_arcUp) <= 0.0f)
            {
                NS::Vector3 normal{};
                if (ProbeFloor(Sphere().WorldSphere().radius / m_params->FloorDot() + m_params->ContactSkin(), normal))
                {
                    Land(normal);
                }
            }
        }

        bool PropActor::ProbeFloor(float distance, NS::Vector3& outNormal)
        {
            const NS::Sphere sphere = Sphere().WorldSphere();
            float hitDistance = 0.0f;
            const NS::Vector3 down = GravityDirection();
            if (!NS::RaycastCollision(this, sphere.center, down, distance, hitDistance, outNormal, Sphere().BodyId()) ||
                NS::Dot(outNormal, -down) < m_params->FloorDot())
            {
                return false;
            }
            const float alignment = NS::Dot(outNormal, -down);
            const float clearance = sphere.radius / alignment;
            if (hitDistance > clearance + 0.01f)
            {
                return false;
            }
            Root().SetPosition(Root().Position() + down * (hitDistance - clearance - m_params->ContactSkin()));
            return true;
        }

        void PropActor::Land(const NS::Vector3& normal)
        {
            // 外れの跳ねが残っていれば、向きを変えて跳ね直し、曲線の重力で落ちる
            if (m_hopsLeft > 0)
            {
                const NS::Vector3 hop =
                    MissHopVelocity(m_velocity, m_arcUp, m_params->MissHop(), m_hopSeed, m_hopIndex);
                if (NS::Dot(hop, m_arcUp) > 0.0f)
                {
                    m_velocity = hop;
                    m_arcDeflected = true;
                    --m_hopsLeft;
                    ++m_hopIndex;
                    m_effects->NotifyLanding(Sphere().WorldSphere().center - normal * Sphere().WorldSphere().radius,
                                             normal);
                    return;
                }
                m_hopsLeft = 0;
            }
            m_velocity -= normal * NS::Dot(m_velocity, normal);
            (void)m_motion.Machine().Change<RollingState>();
            m_effects->NotifyLanding(Sphere().WorldSphere().center - normal * Sphere().WorldSphere().radius, normal);
        }

        void PropActor::StepRolling(float dt)
        {
            NS::Vector3 normal{};
            if (!ProbeFloor(Sphere().WorldSphere().radius / m_params->FloorDot() + 0.01f, normal))
            {
                m_arcDeflected = true;
                (void)m_motion.Machine().Change<ArcState>();
                return;
            }
            m_velocity -= normal * NS::Dot(m_velocity, normal);
            const NS::Vector3 downhill = GravityDirection();
            m_velocity += (downhill - normal * NS::Dot(downhill, normal)) * (m_arc.riseGravity * dt);
            const float speed = m_velocity.Length();
            const float reduced = std::max(0.0f, speed - m_params->Friction() * m_arc.riseGravity * dt);
            if (reduced <= m_params->StopSpeed())
            {
                m_velocity = NS::Vector3{};
                m_motion.Finish();
                return;
            }
            m_velocity *= reduced / speed;
            MoveLaunched(dt);
        }

        void PropActor::MoveLaunched(float dt)
        {
            float remaining = dt;
            for (int contact = 0; contact < m_params->MaxContacts() && remaining > 0.0f; ++contact)
            {
                const float speed = m_velocity.Length();
                if (!(speed > m_params->StopSpeed()))
                {
                    break;
                }
                const NS::Vector3 direction = m_velocity / speed;
                const NS::Sphere sphere = Sphere().WorldSphere();
                const float travel = speed * remaining;
                float distance = 0.0f;
                NS::Vector3 normal{};
                float allowed = travel;
                bool blocked = NS::RaycastCollision(
                    this, sphere.center, direction, travel + sphere.radius * 2.0f, distance, normal, Sphere().BodyId());
                const float approach = -NS::Dot(direction, normal);
                if (blocked && approach > 0.0001f)
                {
                    allowed = std::max(0.0f, distance - sphere.radius / approach - m_params->ContactSkin());
                    blocked = allowed < travel;
                }
                else
                {
                    blocked = false;
                }
                if (!blocked)
                {
                    Root().SetPosition(Root().Position() + m_velocity * remaining);
                    break;
                }
                Root().SetPosition(Root().Position() + direction * allowed);
                remaining -= allowed / speed;
                if (NS::Dot(normal, -GravityDirection()) >= m_params->FloorDot() &&
                    m_motion.Machine().IsCurrent<ArcState>())
                {
                    Land(normal);
                    return;
                }
                const float into = NS::Dot(m_velocity, normal);
                if (into < 0.0f)
                {
                    m_velocity -= normal * ((1.0f + m_params->Restitution()) * into);
                    m_arcDeflected = true;
                }
                Root().SetPosition(Root().Position() + normal * m_params->ContactSkin());
            }
            const NS::Vector3 up = -GravityDirection();
            const NS::Vector3 horizontal = m_velocity - up * NS::Dot(m_velocity, up);
            const NS::Vector3 spin = NS::Cross(up, horizontal) * m_params->SpinPerSpeed();
            const float rate = spin.Length();
            if (std::isfinite(rate) && rate > 0.0001f)
            {
                Root().SetRotation(NS::Quaternion::CreateFromAxisAngle(spin / rate, rate * dt) * Root().Rotation());
            }
        }

        void PropActor::SpawnMark()
        {
            const NS::Vector3 down = GravityDirection();
            const NS::Sphere sphere = Sphere().WorldSphere();
            float distance = 0.0f;
            NS::Vector3 normal{};
            if (NS::RaycastCollision(
                    this, sphere.center, down, m_params->MarkProbeDistance(), distance, normal, Sphere().BodyId()))
            {
                m_marks->Spawn(sphere.center + down * distance + normal * 0.02f);
            }
        }

        void PropActor::ResetTo(const NS::Vector3& position, const NS::Quaternion& rotation)
        {
            if (IsFrozen())
            {
                EndFreeze();
            }
            ModelClassObj()->StopDrawShake();
            m_ghost.Stop(&m_tackleGhost);
            ModelClassObj()->StopDrawTremor();
            // 止めの途中のやり直しでも、置いた所で動ける姿へ戻す。受けた当たりの明けの束は出さない
            OverrideHitStop(0);
            m_received = ReceivedHit{};
            (void)m_states->Change<RestingState>();
            m_states->Reset();
            m_motion.Finish();
            m_velocity = NS::Vector3{};
            m_restAge = 0.0f;
            m_hasLaunched = false;
            Root().SetPosition(position);
            Root().SetRotation(rotation);
            Appear();
            BodySensorClassObj()->Validate();
            m_effects->CancelTrail();
            // 次の歩を待たず、同じ歩の後の問いに置き直した当たりを見せる
            Sphere().SyncToPhysics();
        }

        void PropActor::OnEndPlay()
        {
            if (IsFrozen())
            {
                EndFreeze();
            }
            m_motion.Finish();
            NS::SceneActor::OnEndPlay();
        }

        bool PropActor::ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver)
        {
            if (const MsgAskTackleTarget* ask = NS::MsgCast<MsgAskTackleTarget>(msg))
            {
                if (!IsAppeared() || !BodySensorClassObj()->IsValid())
                {
                    return false;
                }
                TackleTargetAnswer& answer = ask->Answer();
                answer.mass = m_params->Mass();
                answer.toughness = m_params->Toughness();
                // TODO: 壊れる動きは破壊を再開する時に足す。それまで壊れない
                answer.breakable = false;
                answer.placed = !IsFlying();
                answer.position = Root().Position();
                const NS::SensorVolume body = BodySensorClassObj()->WorldVolume();
                answer.bounds = body.Bounds();
                answer.face = m_hitZones->Face();
                answer.body = body;
                answer.launch = m_params->Launch();
                return true;
            }
            if (const MsgTackleHit* hit = NS::MsgCast<MsgTackleHit>(msg))
            {
                // 送る関数はセンサーからセンサーへ送るので、どちらも空でない
                NS_ASSERT(Game, sender != nullptr && receiver != nullptr, "当たったの知らせがセンサーを通らずに届いた");
                if (sender == nullptr || receiver == nullptr)
                {
                    return false;
                }
                ReceiveHit(hit->Desc(), *receiver, *sender);
                return true;
            }
            if (const MsgTackleRelease* release = NS::MsgCast<MsgTackleRelease>(msg))
            {
                // 震えは食い込んだ所から伝わるので、元の位置へ戻す前に始める
                StartReleaseSet();
                Release(release->Desc());
                return true;
            }
            if (IsMsgCourseStart(msg))
            {
                m_coursePose = CoursePose{.position = Root().Position(), .rotation = Root().Rotation()};
                return true;
            }
            if (IsMsgCourseRestart(msg))
            {
                // コースが始まった後に置かれた物は、戻る先を持たない
                if (!m_coursePose.has_value())
                {
                    return false;
                }
                ResetTo(m_coursePose->position, m_coursePose->rotation);
                return true;
            }
            return false;
        }

        NS_PLACEABLE(PropActor, "置物")
    } // namespace Level
} // namespace GL
