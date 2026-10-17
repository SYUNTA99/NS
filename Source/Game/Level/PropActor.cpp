#include "Game/Level/PropActor.h"
#include "NSlib/Core/Types.h"

#include "Game/Level/ImpactMarkActor.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/ImpactTremor.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/CoreObject/ClassObjs/EffectKeeper.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"

#include <algorithm>
#include <cmath>

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

        class PropActor::FreezeState : public NS::StateOf<FreezeState, PropActor>
        {
            void OnStep(PropActor& owner, float dt) override
            {
                if (!owner.m_freezePlaced)
                {
                    owner.m_motion.Step(dt);
                }
                owner.StepFreeze();
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
            NS::Model* model = CreateClassObj(m_model, this, this);
            model->SetMeshRef("sphere");
            model->SetBaseColor(NS::Color{0.72f, 0.70f, 0.66f});
            CreateClassObj(m_shadow, this);
            NS::SphereCollision* sphere = CreateClassObj<NS::Collision, NS::SphereCollision>(m_collision, this);
            CreateClassObj(m_params);
            CreateClassObj(m_hitZones);
            CreateClassObj(m_effectKeeper, this);
            CreateClassObj(m_effects, this);
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
            if (IsAppeared())
            {
                m_tackleGhost.Register();
            }
        }

        void PropActor::OnAppear()
        {
            NS::SceneActor::OnAppear();
            if (IsAppeared())
            {
                m_tackleGhost.Register();
            }
        }

        void PropActor::OnDisappear() noexcept
        {
            NS::SceneActor::OnDisappear();
            m_tackleGhost.Unregister();
        }

        void PropActor::InitAfterPlacement()
        {
            SyncCollision();
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

        void PropActor::BodyStep()
        {
            // 置き直すたびに形を作り直すので、球が変わらない間は置かない。止まっている置物の数だけ毎ステップ確保が走る
            const NS::Sphere sphere = Sphere().WorldSphere();
            if (!m_syncedSphere.has_value() || sphere.center != m_syncedSphere->center ||
                sphere.radius != m_syncedSphere->radius)
            {
                SyncCollision();
            }
        }

        void PropActor::VisualStep()
        {
            // clang-format off
            m_effects->Update(Flight());
            // clang-format on
            // 震えは止めが明けて飛んでいく間に通り抜けるので、状態に依らず根を動かした後で進める
            AdvanceTremor();
            // 明けの写しは止めの後も残り、写し取る姿は揺れを書いた後の姿なので、状態の歩の後で進める
            AdvanceGhost();
        }

        void PropActor::UpdateMotion()
        {
            StateStep();
            BodyStep();
        }

        void PropActor::SyncCollision()
        {
            // Scene に居ない間は入れる先が無い。入れたと控えると、球が変わるまで入れ直さない
            if (IsAppeared() && OwningScene() != nullptr)
            {
                Sphere().SyncToPhysics();
                m_syncedSphere = Sphere().WorldSphere();
            }
        }

        void PropActor::BeginFreeze(const TackleFreezeDesc& desc)
        {
            if (IsFrozen())
            {
                LeaveFreeze();
            }
            m_freezePlaced = !IsFlying();
            m_freezeHome = Root().Position();
            m_freeze = desc;
            m_freeze.squash = false;
            m_freeze.stopSteps = std::max(desc.stopSteps, 0);
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
                        SnapDrawScale(squash);
                    }
                }
            }
            SyncCollision();
        }

        void PropActor::AdvanceShake()
        {
            if (!m_shake.active)
            {
                return;
            }
            ++m_shake.frame;
            const NS::Vector3 offset = m_shake.desc.OffsetAt(m_shake.frame);
            // 有限でないずれは描く形を消すので控えず、前のずれのまま描く
            if (NS::IsFinite(offset))
            {
                m_drawAdjust.offset = offset;
            }
            // 奥揺れは止めの最後のフレームまで揺れるので、長さの次のフレームで 0 を書いてから止める
            if (m_shake.frame > m_shake.desc.length)
            {
                m_shake.active = false;
            }
        }

        void PropActor::StopShake()
        {
            m_shake.active = false;
            m_drawAdjust.offset = NS::Vector3{0.0f, 0.0f, 0.0f};
        }

        void PropActor::AdvanceGhost()
        {
            if (!m_ghost.active)
            {
                return;
            }
            ++m_ghost.frame;
            m_ghost.active = m_ghost.desc.WriteTo(&m_tackleGhost, m_ghost.frame);
        }

        void PropActor::StopGhost()
        {
            m_ghost.active = false;
            (void)m_tackleGhost.SetSpread(NS::Vector3{0.0f, 0.0f, 0.0f});
            (void)m_tackleGhost.SetAfterimageOpacity(0.0f);
        }

        void PropActor::AdvanceTremor()
        {
            if (!m_tremor.active)
            {
                return;
            }
            ++m_tremor.elapsed;
            NS::Gfx::TremorCB tremor{};
            const std::optional<NS::CameraPose> pose = NS::CameraViewPose(this);
            if (pose.has_value())
            {
                // 球の差し渡しで裏まで届く
                tremor = MakeTremor(
                    m_tremor.desc, m_tremor.elapsed, Root().Position(), 2.0f * Sphere().WorldSphere().radius, *pose);
            }
            // 有限でない震えは描く形を消すので控えず、前の震えのまま描く
            if (IsFiniteTremor(tremor))
            {
                m_drawAdjust.tremor = tremor;
            }
            if (m_tremor.elapsed >= m_tremor.desc.length)
            {
                m_tremor.active = false;
            }
        }

        void PropActor::StopTremor()
        {
            m_tremor.active = false;
            m_drawAdjust.tremor = NS::Gfx::TremorCB{};
        }

        void PropActor::EndFreeze()
        {
            StopShake();
            if (m_freezePlaced)
            {
                Root().SetPosition(m_freezeHome);
            }
            if (m_freeze.squash)
            {
                SnapDrawScale(NS::Vector3{1.0f, 1.0f, 1.0f});
                m_freeze.squash = false;
            }
        }

        void PropActor::StepFreeze()
        {
            AdvanceShake();
            if (m_states->StepsInState() >= static_cast<NS::uint32>(std::max(m_freeze.stopSteps, 1)) + 2)
            {
                LeaveFreeze();
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
            SyncCollision();
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
                const float vertical = NS::Dot(m_velocity, m_arcUp);
                float gravity = m_arc.riseGravity;
                if (vertical <= 0.0f)
                {
                    gravity *= m_arc.fallGravityScale;
                }
                if (std::abs(vertical) <= m_arc.apexBandSpeed)
                {
                    gravity *= m_arc.apexBandGravityScale;
                }
                AddGravity(m_velocity, gravity, dt);
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
                (void)ImpactMarkActor::SpawnAt(OwningScene(), sphere.center + down * distance + normal * 0.02f);
            }
        }

        void PropActor::ResetTo(const NS::Vector3& position, const NS::Quaternion& rotation)
        {
            if (IsFrozen())
            {
                EndFreeze();
            }
            StopShake();
            StopGhost();
            StopTremor();
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
            SyncCollision();
        }

        void PropActor::OnEndPlay()
        {
            if (IsFrozen())
            {
                EndFreeze();
            }
            m_motion.Finish();
            NS::SceneActor::OnEndPlay();
            m_tackleGhost.Unregister();
        }

        void PropActor::SnapDrawScale(const NS::Vector3& scale) noexcept
        {
            m_drawAdjust.scale = scale;
            if (NS::Model* model = ModelClassObj())
            {
                model->SnapDrawScale();
            }
        }

        bool PropActor::ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver)
        {
            (void)sender;
            (void)receiver;
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
                answer.hitTimeline = m_params->HitTimelineName();
                return true;
            }
            if (const MsgTackleFreeze* freeze = NS::MsgCast<MsgTackleFreeze>(msg))
            {
                BeginFreeze(freeze->Desc());
                return true;
            }
            if (const MsgTackleShake* shake = NS::MsgCast<MsgTackleShake>(msg))
            {
                m_shake = ShakeRun{.desc = shake->Desc(), .frame = 0, .active = true};
                return true;
            }
            if (const MsgTackleGhost* ghost = NS::MsgCast<MsgTackleGhost>(msg))
            {
                m_ghost = GhostRun{.desc = ghost->Desc(), .frame = 0, .active = true};
                return true;
            }
            if (const MsgTackleTremor* tremor = NS::MsgCast<MsgTackleTremor>(msg))
            {
                m_tremor = TremorRun{.desc = tremor->Desc(), .elapsed = -1, .active = true};
                return true;
            }
            if (const MsgTackleRelease* release = NS::MsgCast<MsgTackleRelease>(msg))
            {
                Release(release->Desc());
                return true;
            }
            if (const MsgCourseRestart* restart = NS::MsgCast<MsgCourseRestart>(msg))
            {
                const nlohmann::json& baseline = restart->Baseline();
                const std::size_t index = NS::FindObjectIndexById(baseline, Id());
                if (index == NS::k_NoObjectIndex)
                {
                    return false;
                }
                const nlohmann::json& placed = NS::SceneJsonObjects(baseline)[index];
                ResetTo(NS::ObjectPosition(placed), NS::ObjectRotation(placed));
                return true;
            }
            return false;
        }

        NS_PLACEABLE(PropActor, "置物")
    } // namespace Level
} // namespace GL
