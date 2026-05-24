#include "Framework/Scene/StaticColliderComponent.h"

#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

namespace NS::Scene
{
    namespace
    {
        [[nodiscard]] NS::Core::Vector3 ClampNonNegative(const NS::Core::Vector3& v) noexcept
        {
            return NS::Core::Vector3{
                v.x < 0.0f ? 0.0f : v.x,
                v.y < 0.0f ? 0.0f : v.y,
                v.z < 0.0f ? 0.0f : v.z,
            };
        }
    } // namespace

    StaticColliderComponent::StaticColliderComponent(const NS::Core::Vector3& halfExtents) noexcept
        : m_halfExtents(ClampNonNegative(halfExtents))
    {}

    StaticColliderComponent::StaticColliderComponent(NS::Scene::GameObject* owner) noexcept : Component(owner) {}

    StaticColliderComponent::StaticColliderComponent(NS::Scene::GameObject* owner,
                                                     const NS::Core::Vector3& halfExtents) noexcept
        : Component(owner), m_halfExtents(ClampNonNegative(halfExtents))
    {}

    void StaticColliderComponent::SetHalfExtents(const NS::Core::Vector3& halfExtents) noexcept
    {
        m_halfExtents = ClampNonNegative(halfExtents);
    }

    NS::Core::Vector3 StaticColliderComponent::HalfExtents() const noexcept
    {
        return m_halfExtents;
    }

    NS::Core::AABB StaticColliderComponent::WorldAABB() const noexcept
    {
        NS::Core::Vector3 center{0.0f, 0.0f, 0.0f};
        if (const GameObject* owner = Owner(); owner != nullptr)
        {
            center = owner->Root().WorldMatrix().Translation();
        }
        return NS::Core::AABB(center, m_halfExtents);
    }
} // namespace NS::Scene
