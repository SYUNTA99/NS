#include "Game/Level/MapObj.h"

#include "Game/Level/ImpactMark.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Gravity.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"

#include <algorithm>
#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        constexpr float k_ContactSkin = 0.001f;
        constexpr float k_StopSpeed = 0.01f;
        constexpr float k_FloorDot = 0.7071f;
        constexpr int k_MaxContacts = 4;

        bool IsFinite(const NS::Core::Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    } // namespace

    class MapObj::RestingState : public NS::Obj::StateOf<RestingState, MapObj>
    {
        void OnEnter(MapObj& owner) override
        {
            owner.m_velocity = NS::Core::Vector3{};
            owner.m_restAge = 0.0f;
        }
        void OnStep(MapObj& owner, float dt) override
        {
            if (owner.m_hasLaunched && owner.m_params.RestLifeSeconds() > 0.0f)
            {
                owner.m_restAge += dt;
                if (owner.m_restAge >= owner.m_params.RestLifeSeconds())
                {
                    owner.BodySensorPart()->Invalidate();
                    if (NS::Phys::PhysicsScene* physics = owner.GetPhysicsScene())
                    {
                        owner.Sphere().RemoveFromPhysics(*physics);
                    }
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
                owner.m_motion.Step(owner, dt);
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
        (void)CreatePart("Model");
        ModelPart()->SetMeshRef("sphere");
        ModelPart()->SetBaseColor(NS::Core::Vector3{0.72f, 0.70f, 0.66f});
        SetCollisionPart(std::make_unique<NS::Obj::SphereCollider>());
        AttachFixedComponent(m_params);
        AttachFixedComponent(m_hitZones);
        AttachFixedComponent(m_effects);
        (void)CreatePart("BodySensor");
        BodySensorPart()->SetType(NS::Obj::HitSensorType::MapObjBody);
        BodySensorPart()->SetSphere(0.5f);
        (void)BuildStateMachine<MapObj, RestingState, FreezeState, LaunchedState>(*this, m_states);
        m_motion.Finish();
    }

    void MapObj::ForEachPart(const PartVisitor& visitor) const
    {
        NS::Obj::Actor::ForEachPart(visitor);
        visitor("Params", const_cast<MapObjParams&>(m_params));
        visitor("HitZones", const_cast<HitZones&>(m_hitZones));
        visitor("LaunchEffects", const_cast<LaunchEffects&>(m_effects));
    }

    void MapObj::InitAfterPlacement()
    {
        BodySensorPart()->SetSphere(Sphere().Radius());
        BodySensorPart()->SetCenterOffset(Sphere().CenterOffset());
        SyncCollider();
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
        if (m_effects.IsActive())
        {
            m_effects.BeginStep();
        }
    }

    void MapObj::VisualStep()
    {
        TickPart(ModelPart());
        TickPart(&m_effects);
    }

    void MapObj::UpdateMotion()
    {
        StepStateMachine();
        // 置き直すたびに形を作り直すので、球が変わらない間は置かない。止まっている置物の数だけ毎ステップ確保が走る
        const NS::Core::Sphere sphere = Sphere().WorldSphere();
        if (!m_hasSyncedSphere || sphere.center != m_syncedSphere.center || sphere.radius != m_syncedSphere.radius)
        {
            SyncCollider();
        }
    }

    void MapObj::SyncCollider()
    {
        if (IsActiveInHierarchy())
        {
            if (NS::Phys::PhysicsScene* physics = GetPhysicsScene())
            {
                Sphere().SyncToPhysics(*physics);
                m_syncedSphere = Sphere().WorldSphere();
                m_hasSyncedSphere = true;
            }
        }
    }

    void MapObj::BeginFreeze(const TackleFreezeDesc& desc)
    {
        if (IsFrozen())
        {
            EndFreeze();
            if (m_freezePlaced || m_motion.IsDead())
            {
                (void)m_states->Change<RestingState>(*this);
            }
            else
            {
                (void)m_states->Change<LaunchedState>(*this);
            }
        }
        m_freezePlaced = !IsFlying();
        m_freezeHome = Root().Position();
        m_freeze = desc;
        m_freeze.squash = false;
        m_freeze.stopSteps = std::max(desc.stopSteps, 0);
        (void)m_states->Change<FreezeState>(*this);
        if (m_freezePlaced)
        {
            Root().SetPosition(m_freezeHome + desc.impactDir * desc.pushInDistance);
            if (desc.squash)
            {
                const float x2 = desc.impactDir.x * desc.impactDir.x;
                const float z2 = desc.impactDir.z * desc.impactDir.z;
                m_freeze.squash =
                    ModelPart()->SnapDrawScale(NS::Core::Vector3{1.0f + (desc.squashThickness - 1.0f) * x2,
                                                                 desc.squashHeight,
                                                                 1.0f + (desc.squashThickness - 1.0f) * z2});
            }
        }
        SyncCollider();
    }

    void MapObj::EndFreeze()
    {
        if (m_freezePlaced)
        {
            Root().SetPosition(m_freezeHome);
        }
        if (m_freeze.squash)
        {
            (void)ModelPart()->SnapDrawScale(NS::Core::Vector3{1.0f, 1.0f, 1.0f});
            m_freeze.squash = false;
        }
    }

    void MapObj::StepFreeze()
    {
        const std::uint32_t step = m_states->StateStep();
        if (step == 0)
        {
            return;
        }
        const int remaining = std::max(m_freeze.stopSteps - static_cast<int>(step), 0);
        if (remaining == 0)
        {
            if (step >= static_cast<std::uint32_t>(std::max(m_freeze.stopSteps, 1)) + 2)
            {
                EndFreeze();
                if (m_freezePlaced || m_motion.IsDead())
                {
                    (void)m_states->Change<RestingState>(*this);
                }
                else
                {
                    (void)m_states->Change<LaunchedState>(*this);
                }
            }
            return;
        }
        if (m_freezePlaced && m_freeze.stopSteps > 0)
        {
            const float sign = 1.0f - 2.0f * static_cast<float>(remaining % 2);
            const float decay = static_cast<float>(remaining) / static_cast<float>(m_freeze.stopSteps);
            Root().SetPosition(m_freezeHome +
                               m_freeze.impactDir * (m_freeze.pushInDistance + m_freeze.shakeAmplitude * sign * decay));
        }
    }

    void MapObj::Release(const TackleReleaseDesc& desc)
    {
        if (IsFrozen())
        {
            EndFreeze();
            if (m_freezePlaced || m_motion.IsDead())
            {
                (void)m_states->Change<RestingState>(*this);
            }
            else
            {
                (void)m_states->Change<LaunchedState>(*this);
            }
        }
        SpawnMark();
        const NS::Core::Vector3 up = -NS::Obj::GravityDirection(*this);
        NS::Core::Vector3 forward = desc.arc.direction - up * NS::Core::Dot(desc.arc.direction, up);
        const float length = forward.Length();
        if (!IsFinite(forward) || !(length > 0.0001f))
        {
            return;
        }
        LaunchArc arc = desc.arc;
        arc.direction = NS::Core::Vector3{1.0f, 0.0f, 0.0f};
        const NS::Core::Vector3 initial = LaunchArcInitialVelocity(arc);
        if (!(initial.Length() > 0.0f))
        {
            return;
        }
        m_arc = arc;
        m_arcUp = up;
        m_arcForward = forward / length;
        m_arcSeconds = 0.0f;
        m_arcDeflected = false;
        m_velocity = m_arcForward * initial.x + m_arcUp * initial.y;
        m_hasLaunched = true;
        m_motion.Build<ArcState, RollingState>(*this);
        (void)m_states->Change<LaunchedState>(*this);
        m_effects.BeginTrail(desc.tier, desc.power, desc.launchScale, desc.arc.direction);
        SyncCollider();
    }

    NS::Core::Vector3 MapObj::ArcOffset(float seconds) const noexcept
    {
        const NS::Core::Vector3 offset = LaunchArcOffsetAt(m_arc, seconds);
        return m_arcForward * offset.x + m_arcUp * offset.y;
    }

    void MapObj::StepLaunched(float dt)
    {
        m_motion.Step(*this, dt);
        if (m_motion.IsDead())
        {
            (void)m_states->Change<RestingState>(*this);
        }
    }

    void MapObj::StepArc(float dt)
    {
        const NS::Core::Vector3 before = ArcOffset(m_arcSeconds);
        m_arcSeconds += dt;
        const NS::Core::Vector3 delta = ArcOffset(m_arcSeconds) - before;
        if (!m_arcDeflected)
        {
            m_velocity = delta / dt;
        }
        else
        {
            const float vertical = NS::Core::Dot(m_velocity, m_arcUp);
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
        if (NS::Core::Dot(m_velocity, m_arcUp) <= 0.0f)
        {
            NS::Core::Vector3 normal{};
            if (ProbeFloor(Sphere().WorldSphere().radius / k_FloorDot + k_ContactSkin, normal))
            {
                Land(normal);
            }
        }
    }

    bool MapObj::ProbeFloor(float distance, NS::Core::Vector3& outNormal)
    {
        const NS::Core::Sphere sphere = Sphere().WorldSphere();
        float hitDistance = 0.0f;
        const NS::Core::Vector3 down = NS::Obj::GravityDirection(*this);
        if (!NS::Obj::RaycastCollision(
                *this, sphere.center, down, distance, hitDistance, outNormal, Sphere().BodyId()) ||
            NS::Core::Dot(outNormal, -down) < k_FloorDot)
        {
            return false;
        }
        const float alignment = NS::Core::Dot(outNormal, -down);
        const float clearance = sphere.radius / alignment;
        if (hitDistance > clearance + 0.01f)
        {
            return false;
        }
        Root().SetPosition(Root().Position() + down * (hitDistance - clearance - k_ContactSkin));
        return true;
    }

    void MapObj::Land(const NS::Core::Vector3& normal)
    {
        m_velocity -= normal * NS::Core::Dot(m_velocity, normal);
        (void)m_motion.Machine().Change<RollingState>(*this);
        m_effects.NotifyLanding(Sphere().WorldSphere().center - normal * Sphere().WorldSphere().radius, normal);
    }

    void MapObj::StepRolling(float dt)
    {
        NS::Core::Vector3 normal{};
        if (!ProbeFloor(Sphere().WorldSphere().radius / k_FloorDot + 0.01f, normal))
        {
            m_arcDeflected = true;
            (void)m_motion.Machine().Change<ArcState>(*this);
            return;
        }
        m_velocity -= normal * NS::Core::Dot(m_velocity, normal);
        const NS::Core::Vector3 downhill = NS::Obj::GravityDirection(*this);
        m_velocity += (downhill - normal * NS::Core::Dot(downhill, normal)) * (m_arc.riseGravity * dt);
        const float speed = m_velocity.Length();
        const float reduced = std::max(0.0f, speed - m_params.Friction() * m_arc.riseGravity * dt);
        if (reduced <= k_StopSpeed)
        {
            m_velocity = NS::Core::Vector3{};
            m_motion.Finish();
            return;
        }
        m_velocity *= reduced / speed;
        MoveLaunched(dt);
    }

    void MapObj::MoveLaunched(float dt)
    {
        float remaining = dt;
        for (int contact = 0; contact < k_MaxContacts && remaining > 0.0f; ++contact)
        {
            const float speed = m_velocity.Length();
            if (!(speed > k_StopSpeed))
            {
                break;
            }
            const NS::Core::Vector3 direction = m_velocity / speed;
            const NS::Core::Sphere sphere = Sphere().WorldSphere();
            const float travel = speed * remaining;
            float distance = 0.0f;
            NS::Core::Vector3 normal{};
            float allowed = travel;
            bool blocked = NS::Obj::RaycastCollision(
                *this, sphere.center, direction, travel + sphere.radius * 2.0f, distance, normal, Sphere().BodyId());
            const float approach = -NS::Core::Dot(direction, normal);
            if (blocked && approach > 0.0001f)
            {
                allowed = std::max(0.0f, distance - sphere.radius / approach - k_ContactSkin);
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
            if (NS::Core::Dot(normal, -NS::Obj::GravityDirection(*this)) >= k_FloorDot &&
                m_motion.Machine().IsCurrent<ArcState>())
            {
                Land(normal);
                return;
            }
            const float into = NS::Core::Dot(m_velocity, normal);
            if (into < 0.0f)
            {
                m_velocity -= normal * ((1.0f + m_params.Restitution()) * into);
                m_arcDeflected = true;
            }
            Root().SetPosition(Root().Position() + normal * k_ContactSkin);
        }
        const NS::Core::Vector3 up = -NS::Obj::GravityDirection(*this);
        const NS::Core::Vector3 horizontal = m_velocity - up * NS::Core::Dot(m_velocity, up);
        const NS::Core::Vector3 spin = NS::Core::Cross(up, horizontal) * m_params.SpinPerSpeed();
        const float rate = spin.Length();
        if (std::isfinite(rate) && rate > 0.0001f)
        {
            Root().SetRotation(NS::Core::Quaternion::CreateFromAxisAngle(spin / rate, rate * dt) * Root().Rotation());
        }
    }

    void MapObj::SpawnMark()
    {
        const NS::Core::Vector3 down = NS::Obj::GravityDirection(*this);
        const NS::Core::Sphere sphere = Sphere().WorldSphere();
        float distance = 0.0f;
        NS::Core::Vector3 normal{};
        if (NS::Obj::RaycastCollision(
                *this, sphere.center, down, m_params.MarkProbeDistance(), distance, normal, Sphere().BodyId()))
        {
            (void)ImpactMark::SpawnAt(OwningScene(), sphere.center + down * distance + normal * 0.02f);
        }
    }

    void MapObj::ResetTo(const NS::Core::Vector3& position, const NS::Core::Quaternion& rotation)
    {
        if (IsFrozen())
        {
            EndFreeze();
        }
        (void)m_states->Change<RestingState>(*this);
        m_states->Reset();
        m_motion.Finish();
        m_velocity = NS::Core::Vector3{};
        m_restAge = 0.0f;
        m_hasLaunched = false;
        Root().SetPosition(position);
        Root().SetRotation(rotation);
        Appear();
        BodySensorPart()->Validate();
        m_effects.CancelTrail();
        SyncCollider();
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
            if (!IsActiveInHierarchy() || !BodySensorPart()->IsValid())
            {
                return false;
            }
            TackleTargetAnswer& answer = ask->Answer();
            answer.mass = m_params.Mass();
            answer.toughness = m_params.Toughness();
            // TODO: 壊れる動きは破壊を再開する時に足す。それまで壊れない
            answer.breakable = false;
            answer.placed = !IsFlying();
            answer.position = Root().Position();
            const NS::Obj::SensorVolume body = BodySensorPart()->WorldVolume();
            answer.bounds = body.Bounds();
            answer.face = m_hitZones.Face();
            answer.body = body;
            return true;
        }
        if (const MsgTackleFreeze* freeze = NS::Obj::MsgCast<MsgTackleFreeze>(msg))
        {
            BeginFreeze(freeze->Desc());
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
