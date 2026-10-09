#include "Game/Level/MapObj.h"

#include "Game/Level/ImpactMark.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/ImpactTremor.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/Object/Gravity.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Level
{
    class MapObj::RestingState : public NS::Obj::StateOf<RestingState, MapObj>
    {
        void OnEnter(MapObj& owner) override
        {
            owner.m_velocity = NS::Vector3{};
            owner.m_restAge = 0.0f;
        }
        void OnStep(MapObj& owner, float dt) override
        {
            if (owner.m_hasLaunched && owner.m_params->RestLifeSeconds() > 0.0f)
            {
                owner.m_restAge += dt;
                if (owner.m_restAge >= owner.m_params->RestLifeSeconds())
                {
                    owner.BodySensorSubObj()->Invalidate();
                    owner.Sphere().RemoveFromPhysics();
                    owner.Kill();
                }
            }
        }
    };

    class MapObj::FreezeState : public NS::Obj::StateOf<FreezeState, MapObj>
    {
        void OnStep(MapObj& owner, float dt) override
        {
            if (!owner.m_freezePlaced)
            {
                owner.m_motion.Step(dt);
            }
            owner.StepFreeze();
        }
    };

    class MapObj::LaunchedState : public NS::Obj::StateOf<LaunchedState, MapObj>
    {
        void OnStep(MapObj& owner, float dt) override { owner.StepLaunched(dt); }
    };

    class MapObj::ArcState : public NS::Obj::StateOf<ArcState, MapObj>
    {
        void OnStep(MapObj& owner, float dt) override { owner.StepArc(dt); }
    };

    class MapObj::RollingState : public NS::Obj::StateOf<RollingState, MapObj>
    {
        void OnStep(MapObj& owner, float dt) override { owner.StepRolling(dt); }
    };

    MapObj::MapObj() noexcept
    {
        m_motion.Finish();
    }

    void MapObj::OnInit()
    {
        NS::Obj::Model* model = CreateSubObj<NS::Obj::Model>(ModelSlot());
        model->SetMeshRef("sphere");
        model->SetBaseColor(NS::Vector3{0.72f, 0.70f, 0.66f});
        NS::Obj::SphereCollision* sphere = CreateSubObj<NS::Obj::SphereCollision>(CollisionSlot());
        m_params = CreateSubObj<MapObjParams>("Params");
        m_hitZones = CreateSubObj<HitZones>("HitZones");
        m_effects = CreateSubObj<LaunchEffects>("LaunchEffects");
        // 体当たりが調べる体は当たりの球そのもの。半径と中心オフセットの正は Collision の欄
        NS::Obj::FollowHitSensor* bodySensor = CreateSubObj<NS::Obj::FollowHitSensor>(BodySensorSlot(), [sphere] {
            const NS::Sphere world = sphere->WorldSphere();
            return NS::Obj::SensorVolume::Sphere(world.center, world.radius);
        });
        SetSensorKind(*bodySensor, SensorKind::MapObjBody);
        (void)BuildStateMachine<MapObj, RestingState, FreezeState, LaunchedState>(*this, m_states);
    }

    void MapObj::InitAfterPlacement()
    {
        SyncCollision();
    }

    bool MapObj::IsFrozen() const noexcept
    {
        return m_states->IsCurrent<FreezeState>();
    }

    bool MapObj::IsFlying() const noexcept
    {
        return m_states->IsCurrent<LaunchedState>() || (IsFrozen() && !m_freezePlaced && !m_motion.IsDead());
    }

    bool MapObj::IsArc() const noexcept
    {
        return IsFlying() && m_motion.Machine().IsCurrent<ArcState>();
    }

    void MapObj::ObserveStep()
    {
        if (m_effects->IsActive())
        {
            m_effects->BeginStep();
        }
    }

    void MapObj::BodyStep()
    {
        // 置き直すたびに形を作り直すので、球が変わらない間は置かない。止まっている置物の数だけ毎ステップ確保が走る
        const NS::Sphere sphere = Sphere().WorldSphere();
        if (!m_syncedSphere.has_value() || sphere.center != m_syncedSphere->center ||
            sphere.radius != m_syncedSphere->radius)
        {
            SyncCollision();
        }
    }

    void MapObj::VisualStep()
    {
        TickSubObj(m_effects);
        // 震えは止めが明けて飛んでいく間に通り抜けるので、状態に依らず根を動かした後で進める
        AdvanceTremor();
    }

    void MapObj::UpdateMotion()
    {
        StateStep();
        BodyStep();
    }

    void MapObj::SyncCollision()
    {
        // Scene に居ない間は入れる先が無い。入れたと控えると、球が変わるまで入れ直さない
        if (IsActiveInHierarchy() && OwningScene() != nullptr)
        {
            Sphere().SyncToPhysics();
            m_syncedSphere = Sphere().WorldSphere();
        }
    }

    void MapObj::BeginFreeze(const TackleFreezeDesc& desc)
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
                m_freeze.squash = ModelSubObj()->SnapDrawScale(NS::Vector3{1.0f + (desc.squashThickness - 1.0f) * x2,
                                                                           desc.squashHeight,
                                                                           1.0f + (desc.squashThickness - 1.0f) * z2});
            }
        }
        SyncCollision();
    }

    void MapObj::AdvanceShake()
    {
        if (!m_shake.active || ModelSubObj() == nullptr)
        {
            return;
        }
        ++m_shake.frame;
        const TackleShakeDesc& desc = m_shake.desc;
        const float offset =
            BodyShakeOffset(m_shake.frame, desc.length, desc.amplitude, desc.seed, desc.firstSign, desc.flipFrames);
        (void)ModelSubObj()->SetDrawOffset(desc.axis * offset);
        (void)ModelSubObj()->SetGhostSpread(
            desc.axis * (BodyShakeReach(m_shake.frame, desc.length, desc.amplitude) * desc.ghostRatio));
        if (m_shake.frame >= desc.length)
        {
            m_shake.active = false;
        }
    }

    void MapObj::StopShake()
    {
        m_shake.active = false;
        if (ModelSubObj() != nullptr)
        {
            (void)ModelSubObj()->SetDrawOffset(NS::Vector3{0.0f, 0.0f, 0.0f});
            (void)ModelSubObj()->SetGhostSpread(NS::Vector3{0.0f, 0.0f, 0.0f});
        }
    }

    void MapObj::AdvanceTremor()
    {
        if (!m_tremor.active || ModelSubObj() == nullptr)
        {
            return;
        }
        ++m_tremor.elapsed;
        NS::Gfx::TremorCB tremor{};
        const std::optional<NS::Obj::CameraPose> pose = NS::Obj::CameraViewPose(*this);
        if (pose.has_value())
        {
            // 球の差し渡しで裏まで届く
            tremor = MakeTremor(
                m_tremor.desc, m_tremor.elapsed, Root().Position(), 2.0f * Sphere().WorldSphere().radius, *pose);
        }
        (void)ModelSubObj()->SetTremor(tremor);
        if (m_tremor.elapsed >= m_tremor.desc.length)
        {
            m_tremor.active = false;
        }
    }

    void MapObj::StopTremor()
    {
        m_tremor.active = false;
        if (ModelSubObj() != nullptr)
        {
            (void)ModelSubObj()->SetTremor(NS::Gfx::TremorCB{});
        }
    }

    void MapObj::EndFreeze()
    {
        StopShake();
        if (m_freezePlaced)
        {
            Root().SetPosition(m_freezeHome);
        }
        if (m_freeze.squash)
        {
            (void)ModelSubObj()->SnapDrawScale(NS::Vector3{1.0f, 1.0f, 1.0f});
            m_freeze.squash = false;
        }
    }

    void MapObj::StepFreeze()
    {
        AdvanceShake();
        if (m_states->StepsInState() >= static_cast<std::uint32_t>(std::max(m_freeze.stopSteps, 1)) + 2)
        {
            LeaveFreeze();
        }
    }

    void MapObj::LeaveFreeze()
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

    void MapObj::Release(const TackleReleaseDesc& desc)
    {
        if (IsFrozen())
        {
            LeaveFreeze();
        }
        SpawnMark();
        const NS::Vector3 up = -NS::Obj::GravityDirection(*this);
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
        m_effects->BeginTrail(desc.tier, desc.power, desc.launchScale, desc.arc.direction);
        SyncCollision();
    }

    NS::Vector3 MapObj::ArcOffset(float seconds) const noexcept
    {
        const NS::Vector3 offset = LaunchArcOffsetAt(m_arc, seconds);
        return m_arcForward * offset.x + m_arcUp * offset.y;
    }

    void MapObj::StepLaunched(float dt)
    {
        m_motion.Step(dt);
        if (m_motion.IsDead())
        {
            (void)m_states->Change<RestingState>();
        }
    }

    void MapObj::StepArc(float dt)
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
            NS::Obj::AddGravity(*this, m_velocity, gravity, dt);
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

    bool MapObj::ProbeFloor(float distance, NS::Vector3& outNormal)
    {
        const NS::Sphere sphere = Sphere().WorldSphere();
        float hitDistance = 0.0f;
        const NS::Vector3 down = NS::Obj::GravityDirection(*this);
        if (!NS::Obj::RaycastCollision(
                *this, sphere.center, down, distance, hitDistance, outNormal, Sphere().BodyId()) ||
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

    void MapObj::Land(const NS::Vector3& normal)
    {
        // 外れの跳ねが残っていれば、向きを変えて跳ね直し、曲線の重力で落ちる
        if (m_hopsLeft > 0)
        {
            const NS::Vector3 hop = MissHopVelocity(m_velocity, m_arcUp, m_params->MissHop(), m_hopSeed, m_hopIndex);
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

    void MapObj::StepRolling(float dt)
    {
        NS::Vector3 normal{};
        if (!ProbeFloor(Sphere().WorldSphere().radius / m_params->FloorDot() + 0.01f, normal))
        {
            m_arcDeflected = true;
            (void)m_motion.Machine().Change<ArcState>();
            return;
        }
        m_velocity -= normal * NS::Dot(m_velocity, normal);
        const NS::Vector3 downhill = NS::Obj::GravityDirection(*this);
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

    void MapObj::MoveLaunched(float dt)
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
            bool blocked = NS::Obj::RaycastCollision(
                *this, sphere.center, direction, travel + sphere.radius * 2.0f, distance, normal, Sphere().BodyId());
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
            if (NS::Dot(normal, -NS::Obj::GravityDirection(*this)) >= m_params->FloorDot() &&
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
        const NS::Vector3 up = -NS::Obj::GravityDirection(*this);
        const NS::Vector3 horizontal = m_velocity - up * NS::Dot(m_velocity, up);
        const NS::Vector3 spin = NS::Cross(up, horizontal) * m_params->SpinPerSpeed();
        const float rate = spin.Length();
        if (std::isfinite(rate) && rate > 0.0001f)
        {
            Root().SetRotation(NS::Quaternion::CreateFromAxisAngle(spin / rate, rate * dt) * Root().Rotation());
        }
    }

    void MapObj::SpawnMark()
    {
        const NS::Vector3 down = NS::Obj::GravityDirection(*this);
        const NS::Sphere sphere = Sphere().WorldSphere();
        float distance = 0.0f;
        NS::Vector3 normal{};
        if (NS::Obj::RaycastCollision(
                *this, sphere.center, down, m_params->MarkProbeDistance(), distance, normal, Sphere().BodyId()))
        {
            (void)ImpactMark::SpawnAt(OwningScene(), sphere.center + down * distance + normal * 0.02f);
        }
    }

    void MapObj::ResetTo(const NS::Vector3& position, const NS::Quaternion& rotation)
    {
        if (IsFrozen())
        {
            EndFreeze();
        }
        StopShake();
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
        BodySensorSubObj()->Validate();
        m_effects->CancelTrail();
        SyncCollision();
    }

    void MapObj::OnEndPlay()
    {
        if (IsFrozen())
        {
            EndFreeze();
        }
        m_motion.Finish();
        NS::Obj::Actor::OnEndPlay();
    }

    bool MapObj::ReceiveMsg(const NS::Obj::Message& msg, NS::Obj::HitSensor* sender, NS::Obj::HitSensor* receiver)
    {
        (void)sender;
        (void)receiver;
        if (const MsgAskTackleTarget* ask = NS::Obj::MsgCast<MsgAskTackleTarget>(msg))
        {
            if (!IsActiveInHierarchy() || !BodySensorSubObj()->IsValid())
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
            const NS::Obj::SensorVolume body = BodySensorSubObj()->WorldVolume();
            answer.bounds = body.Bounds();
            answer.face = m_hitZones->Face();
            answer.body = body;
            return true;
        }
        if (const MsgTackleFreeze* freeze = NS::Obj::MsgCast<MsgTackleFreeze>(msg))
        {
            BeginFreeze(freeze->Desc());
            return true;
        }
        if (const MsgTackleShake* shake = NS::Obj::MsgCast<MsgTackleShake>(msg))
        {
            m_shake = ShakeRun{.desc = shake->Desc(), .frame = 0, .active = true};
            return true;
        }
        if (const MsgTackleTremor* tremor = NS::Obj::MsgCast<MsgTackleTremor>(msg))
        {
            m_tremor = TremorRun{.desc = tremor->Desc(), .elapsed = -1, .active = true};
            return true;
        }
        if (const MsgTackleRelease* release = NS::Obj::MsgCast<MsgTackleRelease>(msg))
        {
            Release(release->Desc());
            return true;
        }
        if (const MsgCourseRestart* restart = NS::Obj::MsgCast<MsgCourseRestart>(msg))
        {
            const nlohmann::json& baseline = restart->Baseline();
            const std::size_t index = NS::Obj::FindObjectIndexById(baseline, Id());
            if (index == NS::Obj::k_NoObjectIndex)
            {
                return false;
            }
            const nlohmann::json& placed = NS::Obj::SceneJsonObjects(baseline)[index];
            ResetTo(NS::Obj::ObjectPosition(placed), NS::Obj::ObjectRotation(placed));
            return true;
        }
        return false;
    }

    NS_PLACEABLE(MapObj, "置物")
} // namespace NS::Game::Level
