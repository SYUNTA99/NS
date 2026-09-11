#include "Runtime/Object/Components/ColliderComponent.h"

#include "Runtime/Physics/PhysicsWorld.h"

namespace NS::Object
{
    void ColliderComponent::TrackBody(NS::Physics::PhysicsWorld& physics, JPH::BodyID body) noexcept
    {
        if (m_physics != nullptr && (m_physics != &physics || m_bodyId != body))
            m_physics->RemoveBody(m_bodyId);
        m_physics = &physics;
        m_bodyId = body;
    }

    JPH::BodyID ColliderComponent::BodyIn(const NS::Physics::PhysicsWorld& physics) const noexcept
    {
        return m_physics == &physics ? m_bodyId : JPH::BodyID{};
    }

    void ColliderComponent::RemoveFromPhysics() noexcept
    {
        if (m_physics == nullptr)
            return;

        m_physics->RemoveBody(m_bodyId);
        m_bodyId = JPH::BodyID{};
    }

    void ColliderComponent::OnEndPlay()
    {
        RemoveFromPhysics();
    }
} // namespace NS::Object
