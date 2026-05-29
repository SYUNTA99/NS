#include "Framework/Scene/ClimbableSurfaceComponent.h"

#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

namespace NS::Scene
{
    ClimbableSurfaceComponent::ClimbableSurfaceComponent(GameObject* owner,
                                                         ClimbableKind kind,
                                                         const NS::Core::Vector3& halfExtents,
                                                         const NS::Core::Vector3& faceNormal) noexcept
        : Component(owner), m_kind(kind), m_halfExtents(halfExtents), m_faceNormal(faceNormal)
    {
        if (m_halfExtents.x < 0.0f)
            m_halfExtents.x = 0.0f;
        if (m_halfExtents.y < 0.0f)
            m_halfExtents.y = 0.0f;
        if (m_halfExtents.z < 0.0f)
            m_halfExtents.z = 0.0f;
    }

    NS::Core::AABB ClimbableSurfaceComponent::WorldAABB() const noexcept
    {
        NS::Core::Vector3 center{0.0f, 0.0f, 0.0f};
        if (const GameObject* owner = Owner(); owner != nullptr)
        {
            center = owner->Root().WorldMatrix().Translation();
        }
        return NS::Core::AABB(center, m_halfExtents);
    }

    bool ClimbableSurfaceComponent::ContainsPoint(const NS::Core::Vector3& worldPos) const noexcept
    {
        const NS::Core::AABB box = WorldAABB();
        return box.Contains(worldPos) != DirectX::DISJOINT;
    }
} // namespace NS::Scene
