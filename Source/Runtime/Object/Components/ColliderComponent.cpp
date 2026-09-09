#include "Runtime/Object/Components/ColliderComponent.h"

#include "Runtime/Physics/JoltWorld.h"

namespace NS::Object
{
    void ColliderComponent::ReplaceBody(NS::Physics::JoltWorld& physics, JPH::BodyID created) noexcept
    {
        physics.RemoveBody(m_bodyId);
        m_bodyId = created;
    }
} // namespace NS::Object
