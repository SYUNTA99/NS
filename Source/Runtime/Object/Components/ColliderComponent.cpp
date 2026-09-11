#include "Runtime/Object/Components/ColliderComponent.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Physics/PhysicsWorld.h"

namespace NS::Object
{
    void ColliderComponent::SyncToPhysics(NS::Physics::PhysicsWorld& physics)
    {
        if (!AcceptsWorld(physics))
            return;

        const JPH::BodyID body = SyncBody(physics, m_bodyId);
        // 置き直しは同じ id を返す。 違うのは初めて作った時か、 無効が返った時だけ
        if (m_bodyId != body)
            physics.RemoveBody(m_bodyId);
        m_bodyId = body;
    }

    void ColliderComponent::RemoveFromPhysics(NS::Physics::PhysicsWorld& physics)
    {
        if (!AcceptsWorld(physics))
            return;

        physics.RemoveBody(m_bodyId);
        m_bodyId = JPH::BodyID{};
    }

    void ColliderComponent::OnEndPlay()
    {
        if (m_bodyId.IsInvalid())
            return;

        NS::Physics::PhysicsWorld* sceneWorld = SceneWorld();
        if (sceneWorld == nullptr)
        {
            NS_LOG_ERROR(Scene, "ColliderComponent: Scene に居ないので body を外せない。 body は world を壊すまで残る");
            m_bodyId = JPH::BodyID{};
            return;
        }
        RemoveFromPhysics(*sceneWorld);
    }

    NS::Physics::PhysicsWorld* ColliderComponent::SceneWorld() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr || owner->OwningScene() == nullptr)
            return nullptr;
        return &owner->OwningScene()->Physics();
    }

    bool ColliderComponent::AcceptsWorld(const NS::Physics::PhysicsWorld& physics) const
    {
        const NS::Physics::PhysicsWorld* sceneWorld = SceneWorld();
        if (sceneWorld == nullptr || sceneWorld == &physics)
            return true;

        NS_LOG_ERROR(Scene, "ColliderComponent: 持ち主の Scene と違う world を渡された。 body を出し入れしない");
        return false;
    }
} // namespace NS::Object
