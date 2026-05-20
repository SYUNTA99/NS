#include "ns/scene/components/static_collider_component.h"

#include "ns/scene/game_object.h"
#include "ns/scene/transform.h"

namespace ns::scene
{
    StaticColliderComponent::StaticColliderComponent(const ns::core::Vector3& halfExtents) noexcept
        : m_halfExtents(halfExtents)
    {}

    void StaticColliderComponent::SetHalfExtents(const ns::core::Vector3& halfExtents) noexcept
    {
        m_halfExtents = halfExtents;
    }

    ns::core::Vector3 StaticColliderComponent::HalfExtents() const noexcept
    {
        return m_halfExtents;
    }

    ns::core::AABB StaticColliderComponent::WorldAABB() const noexcept
    {
        ns::core::Vector3 center{0.0f, 0.0f, 0.0f};
        if (const GameObject* owner = Owner(); owner != nullptr)
        {
            center = owner->Root().WorldMatrix().Translation();
        }
        return ns::core::AABB(center, m_halfExtents);
    }
} // namespace ns::scene
