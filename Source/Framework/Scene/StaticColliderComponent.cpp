#include "Framework/Scene/StaticColliderComponent.h"

#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

namespace NS::Scene
{
    namespace
    {
        [[nodiscard]] NS::Math::Vector3 ClampNonNegative(const NS::Math::Vector3& v) noexcept
        {
            return NS::Math::Vector3{
                v.x < 0.0f ? 0.0f : v.x,
                v.y < 0.0f ? 0.0f : v.y,
                v.z < 0.0f ? 0.0f : v.z,
            };
        }
    } // namespace

    StaticColliderComponent::StaticColliderComponent() noexcept {}

    StaticColliderComponent::StaticColliderComponent(const NS::Math::Vector3& halfExtents) noexcept
        : m_halfExtents(ClampNonNegative(halfExtents))
    {}

    void StaticColliderComponent::SetHalfExtents(const NS::Math::Vector3& halfExtents) noexcept
    {
        m_halfExtents = ClampNonNegative(halfExtents);
    }

    NS::Math::Vector3 StaticColliderComponent::HalfExtents() const noexcept
    {
        return m_halfExtents;
    }

    NS::Math::AABB StaticColliderComponent::WorldAABB() const noexcept
    {
        NS::Math::Vector3 center{0.0f, 0.0f, 0.0f};
        if (const GameObject* owner = Owner(); owner != nullptr)
        {
            center = owner->Root().WorldMatrix().Translation();
        }
        return NS::Math::AABB(center, m_halfExtents);
    }
} // namespace NS::Scene
