#include "Runtime/Object/Components/Collider.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Physics/PhysicsScene.h"

namespace NS::Obj
{
    void Collider::SyncToPhysics(NS::Phys::PhysicsScene& physics)
    {
        if (!AcceptsScenePhysics(physics))
        {
            return;
        }

        // 形は RigidBody が body へまとめる。自分の body を残すと、同じ場所に静的な当たりが二重に立つ
        if (JoinsRigidBody())
        {
            physics.RemoveBody(m_bodyId);
            m_bodyId = JPH::BodyID{};
            return;
        }

        const JPH::BodyID body = SyncBody(physics, m_bodyId);
        // 置き直しは同じ id を返す。違うのは初めて作った時か、無効が返った時だけ
        if (m_bodyId != body)
        {
            physics.RemoveBody(m_bodyId);
        }
        m_bodyId = body;
    }

    JPH::BodyID Collider::BodyId() const noexcept
    {
        if (m_bodyId.IsInvalid() && JoinsRigidBody())
        {
            return Owner()->FindComponent<RigidBody>()->BodyId();
        }
        return m_bodyId;
    }

    bool Collider::JoinsRigidBody() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr || !CanJoinRigidBody())
        {
            return false;
        }
        const RigidBody* rigidBody = owner->FindComponent<RigidBody>();
        return rigidBody != nullptr && rigidBody->IsActive();
    }

    void Collider::RemoveFromPhysics(NS::Phys::PhysicsScene& physics)
    {
        if (!AcceptsScenePhysics(physics))
        {
            return;
        }

        physics.RemoveBody(m_bodyId);
        m_bodyId = JPH::BodyID{};
    }

    void Collider::OnEndPlay()
    {
        if (m_bodyId.IsInvalid())
        {
            return;
        }

        NS::Phys::PhysicsScene* scenePhysics = ScenePhysics();
        if (scenePhysics == nullptr)
        {
            NS_LOG_ERROR(Scene, "Collider: Scene に居ないので body を外せない。 body は PhysicsScene を壊すまで残る");
            m_bodyId = JPH::BodyID{};
            return;
        }
        RemoveFromPhysics(*scenePhysics);
    }

    NS::Phys::PhysicsScene* Collider::ScenePhysics() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr || owner->OwningScene() == nullptr)
        {
            return nullptr;
        }
        return &owner->OwningScene()->Physics();
    }

    bool Collider::AcceptsScenePhysics(const NS::Phys::PhysicsScene& physics) const
    {
        const NS::Phys::PhysicsScene* scenePhysics = ScenePhysics();
        if (scenePhysics == nullptr)
        {
            NS_LOG_ERROR(Scene, "Collider: 持ち主が Scene に居ないので body を出し入れしない");
            return false;
        }
        if (scenePhysics == &physics)
        {
            return true;
        }

        NS_LOG_ERROR(Scene, "Collider: 持ち主の Scene と違う PhysicsScene を渡された。 body を出し入れしない");
        return false;
    }
} // namespace NS::Obj
