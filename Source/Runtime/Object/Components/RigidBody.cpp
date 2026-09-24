#include "Runtime/Object/Components/RigidBody.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Platform/Clock.h"

namespace NS::Obj
{
    void RigidBody::SyncToPhysics(NS::Phys::PhysicsScene& physics)
    {
        if (!AcceptsScenePhysics(physics))
        {
            return;
        }

        std::vector<NS::Phys::ShapePart> parts;
        bool leftStatic = false;
        for (Component* comp : Owner()->Components())
        {
            const Collider* collider = ComponentCast<Collider>(comp);
            if (collider == nullptr || !collider->IsActive())
            {
                continue;
            }
            if (collider->CanJoinRigidBody())
            {
                parts.push_back(collider->RigidBodyPart());
            }
            else if (!collider->FollowsRigidBody())
            {
                leftStatic = true;
            }
        }
        if (leftStatic)
        {
            NS_LOG_WARN(Scene,
                        "RigidBody: '{}' のメッシュ・スロープの当たりは動く body の形にできない。静的なまま元の場所に残る",
                        Owner()->Name());
        }

        NS::Core::Vector3 position;
        NS::Core::Quaternion rotation;
        OwnerWorldPose(position, rotation);
        const NS::Phys::BodyMotion motion = Motion();
        const JPH::BodyID body = physics.SyncMovingBody(m_bodyId, parts, position, rotation, motion);
        // 置き直しは同じ id を返す。違うのは初めて作った時か、無効が返った時だけ
        if (m_bodyId != body)
        {
            physics.RemoveBody(m_bodyId);
        }
        m_bodyId = body;
        m_appliedMotion = motion;
        if (m_bodyId.IsInvalid())
        {
            NS_LOG_WARN(Scene,
                        "RigidBody: '{}' に形になる箱・球・カプセルの collider が無いので body を作らない",
                        Owner()->Name());
            return;
        }

        m_followedWorld = RootTransform().WorldMatrix();
    }

    void RigidBody::RemoveFromPhysics(NS::Phys::PhysicsScene& physics)
    {
        if (!AcceptsScenePhysics(physics))
        {
            return;
        }

        physics.RemoveBody(m_bodyId);
        m_bodyId = JPH::BodyID{};
    }

    void RigidBody::PrePhysicsStep()
    {
        NS::Phys::PhysicsScene* physics = ScenePhysics();
        if (physics == nullptr || m_bodyId.IsInvalid())
        {
            return;
        }

        // エディタの欄は setter を通らずに書き換わる。毎歩見比べ、変わった時だけ body へ入れる
        const NS::Phys::BodyMotion motion = Motion();
        if (!(motion == m_appliedMotion))
        {
            physics->SetBodyMotion(m_bodyId, motion);
            m_appliedMotion = motion;
        }

        if (m_kinematic)
        {
            NS::Core::Vector3 position;
            NS::Core::Quaternion rotation;
            OwnerWorldPose(position, rotation);
            physics->MoveKinematic(m_bodyId, position, rotation, NS::Platform::FrameTimer::FixedDelta());
        }
    }

    void RigidBody::PostPhysicsStep()
    {
        NS::Phys::PhysicsScene* physics = ScenePhysics();
        if (physics == nullptr || m_bodyId.IsInvalid())
        {
            return;
        }

        // キネマティックの姿勢は Transform が正。書き戻すと運んだ先の丸め誤差が Transform へ溜まる
        if (!m_kinematic)
        {
            SetOwnerWorldPose(physics->BodyPosition(m_bodyId), physics->BodyRotation(m_bodyId));
        }
        SyncFollowers(*physics);
    }

    void RigidBody::OnStart()
    {
        if (NS::Phys::PhysicsScene* physics = ScenePhysics())
        {
            SyncToPhysics(*physics);
        }
    }

    void RigidBody::OnEndPlay()
    {
        if (m_bodyId.IsInvalid())
        {
            return;
        }

        NS::Phys::PhysicsScene* physics = ScenePhysics();
        if (physics == nullptr)
        {
            NS_LOG_ERROR(Scene, "RigidBody: Scene に居ないので body を外せない。body は PhysicsScene を壊すまで残る");
            m_bodyId = JPH::BodyID{};
            return;
        }
        RemoveFromPhysics(*physics);
    }

    void RigidBody::LockPosition(bool x, bool y, bool z) noexcept
    {
        m_lockPositionX = x;
        m_lockPositionY = y;
        m_lockPositionZ = z;
    }

    void RigidBody::LockRotation(bool x, bool y, bool z) noexcept
    {
        m_lockRotationX = x;
        m_lockRotationY = y;
        m_lockRotationZ = z;
    }

    NS::Phys::BodyMotion RigidBody::Motion() const noexcept
    {
        NS::Phys::BodyMotion motion;
        motion.kinematic = m_kinematic;
        motion.mass = m_mass;
        motion.friction = m_friction;
        motion.restitution = m_restitution;
        motion.linearDamping = m_linearDamping;
        motion.angularDamping = m_angularDamping;
        motion.gravityFactor = m_useGravity ? m_gravityScale : 0.0f;
        motion.continuousCollision = m_continuousCollision;

        JPH::EAllowedDOFs allowed = JPH::EAllowedDOFs::All;
        const auto lockAxis = [&allowed](bool locked, JPH::EAllowedDOFs axis) noexcept {
            if (locked)
            {
                allowed = allowed & ~axis;
            }
        };
        lockAxis(m_lockPositionX, JPH::EAllowedDOFs::TranslationX);
        lockAxis(m_lockPositionY, JPH::EAllowedDOFs::TranslationY);
        lockAxis(m_lockPositionZ, JPH::EAllowedDOFs::TranslationZ);
        lockAxis(m_lockRotationX, JPH::EAllowedDOFs::RotationX);
        lockAxis(m_lockRotationY, JPH::EAllowedDOFs::RotationY);
        lockAxis(m_lockRotationZ, JPH::EAllowedDOFs::RotationZ);
        motion.allowedDOFs = allowed;
        return motion;
    }

    NS::Core::Vector3 RigidBody::Velocity() const
    {
        const NS::Phys::PhysicsScene* physics = ScenePhysics();
        if (physics == nullptr || m_bodyId.IsInvalid())
        {
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        }
        return physics->BodyVelocity(m_bodyId);
    }

    void RigidBody::SetVelocity(const NS::Core::Vector3& velocity)
    {
        if (NS::Phys::PhysicsScene* physics = ScenePhysics())
        {
            physics->SetBodyVelocity(m_bodyId, velocity);
        }
    }

    NS::Core::Vector3 RigidBody::AngularVelocity() const
    {
        const NS::Phys::PhysicsScene* physics = ScenePhysics();
        if (physics == nullptr || m_bodyId.IsInvalid())
        {
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        }
        return physics->BodyAngularVelocity(m_bodyId);
    }

    void RigidBody::SetAngularVelocity(const NS::Core::Vector3& angularVelocity)
    {
        if (NS::Phys::PhysicsScene* physics = ScenePhysics())
        {
            physics->SetBodyAngularVelocity(m_bodyId, angularVelocity);
        }
    }

    void RigidBody::AddForce(const NS::Core::Vector3& force)
    {
        if (NS::Phys::PhysicsScene* physics = ScenePhysics())
        {
            physics->AddBodyForce(m_bodyId, force);
        }
    }

    void RigidBody::AddImpulse(const NS::Core::Vector3& impulse)
    {
        if (NS::Phys::PhysicsScene* physics = ScenePhysics())
        {
            physics->AddBodyImpulse(m_bodyId, impulse);
        }
    }

    void RigidBody::AddTorque(const NS::Core::Vector3& torque)
    {
        if (NS::Phys::PhysicsScene* physics = ScenePhysics())
        {
            physics->AddBodyTorque(m_bodyId, torque);
        }
    }

    void RigidBody::AddAngularImpulse(const NS::Core::Vector3& angularImpulse)
    {
        if (NS::Phys::PhysicsScene* physics = ScenePhysics())
        {
            physics->AddBodyAngularImpulse(m_bodyId, angularImpulse);
        }
    }

    bool RigidBody::IsSleeping() const
    {
        const NS::Phys::PhysicsScene* physics = ScenePhysics();
        if (physics == nullptr || m_bodyId.IsInvalid())
        {
            return false;
        }
        return !physics->IsBodyAwake(m_bodyId);
    }

    void RigidBody::WakeUp()
    {
        if (NS::Phys::PhysicsScene* physics = ScenePhysics())
        {
            physics->WakeBody(m_bodyId);
        }
    }

    std::vector<NS::Phys::BodyContact> RigidBody::Contacts() const
    {
        const NS::Phys::PhysicsScene* physics = ScenePhysics();
        if (physics == nullptr)
        {
            return {};
        }
        return physics->ContactsOf(m_bodyId);
    }

    void RigidBody::Teleport(const NS::Core::Vector3& position, const NS::Core::Quaternion& rotation)
    {
        if (Owner() == nullptr)
        {
            return;
        }

        SetOwnerWorldPose(position, rotation);
        NS::Phys::PhysicsScene* physics = ScenePhysics();
        if (physics == nullptr || m_bodyId.IsInvalid())
        {
            return;
        }

        // body の原点は持ち主の姿勢そのもの。親で直した後の値を読み直すので、Transform と body がずれない
        NS::Core::Vector3 worldPosition;
        NS::Core::Quaternion worldRotation;
        OwnerWorldPose(worldPosition, worldRotation);
        physics->TeleportBody(m_bodyId, worldPosition, worldRotation);
        SyncFollowers(*physics);
    }

    NS::Phys::PhysicsScene* RigidBody::ScenePhysics() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr || owner->OwningScene() == nullptr)
        {
            return nullptr;
        }
        return &owner->OwningScene()->Physics();
    }

    bool RigidBody::AcceptsScenePhysics(const NS::Phys::PhysicsScene& physics) const
    {
        const NS::Phys::PhysicsScene* scenePhysics = ScenePhysics();
        if (scenePhysics == nullptr)
        {
            NS_LOG_ERROR(Scene, "RigidBody: 持ち主が Scene に居ないので body を出し入れしない");
            return false;
        }
        if (scenePhysics == &physics)
        {
            return true;
        }

        NS_LOG_ERROR(Scene, "RigidBody: 持ち主の Scene と違う PhysicsScene を渡された。body を出し入れしない");
        return false;
    }

    void RigidBody::OwnerWorldPose(NS::Core::Vector3& position, NS::Core::Quaternion& rotation) const noexcept
    {
        const NS::Core::AffineDecomposition parts = NS::Core::DecomposeAffine(RootTransform().WorldMatrix());
        position = parts.translation;
        rotation = parts.rotation;
    }

    void RigidBody::SetOwnerWorldPose(const NS::Core::Vector3& position,
                                               const NS::Core::Quaternion& rotation) noexcept
    {
        Transform& root = RootTransform();
        const Transform* parent = root.Parent();
        if (parent == nullptr)
        {
            root.SetPosition(position);
            root.SetRotation(rotation);
            return;
        }

        const NS::Core::Matrix world =
            NS::Core::Matrix::CreateFromQuaternion(rotation) * NS::Core::Matrix::CreateTranslation(position);
        const NS::Core::AffineDecomposition local = NS::Core::DecomposeAffine(world * parent->WorldMatrix().Invert());
        root.SetPosition(local.translation);
        root.SetRotation(local.rotation);
    }

    void RigidBody::SyncFollowers(NS::Phys::PhysicsScene& physics)
    {
        // 止まっている間も毎歩置き直すと、トリガーの箱を毎歩作り直すことになる
        const NS::Core::Matrix world = RootTransform().WorldMatrix();
        if (world == m_followedWorld)
        {
            return;
        }
        m_followedWorld = world;

        for (Component* comp : Owner()->Components())
        {
            Collider* collider = ComponentCast<Collider>(comp);
            if (collider != nullptr && collider->IsActive() && !collider->CanJoinRigidBody() &&
                collider->FollowsRigidBody())
            {
                collider->SyncToPhysics(physics);
            }
        }
    }

    NS_CLASS(RigidBody)
} // namespace NS::Obj
