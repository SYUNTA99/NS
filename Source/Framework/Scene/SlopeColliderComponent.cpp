#include "Framework/Scene/SlopeColliderComponent.h"

#include "Framework/Physics/WedgeGeometry.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

namespace NS::Scene
{
    SlopeColliderComponent::SlopeColliderComponent(NS::Scene::GameObject* owner,
                                                   float angleDegrees,
                                                   const NS::Core::Vector3& halfExtents) noexcept
        : Component(owner), m_angleDegrees(angleDegrees), m_halfExtents(halfExtents)
    {
        if (m_halfExtents.x < 0.0f)
            m_halfExtents.x = 0.0f;
        if (m_halfExtents.y < 0.0f)
            m_halfExtents.y = 0.0f;
        if (m_halfExtents.z < 0.0f)
            m_halfExtents.z = 0.0f;
    }

    std::array<NS::Physics::Triangle, 8> SlopeColliderComponent::WorldTriangles() const noexcept
    {
        // local (rotation は Owner transform に載るので yaw=0)。 向き・位置は world matrix で反映。
        auto tris = NS::Physics::BuildWedgeTriangles({0.0f, 0.0f, 0.0f}, m_halfExtents, m_angleDegrees, 0.0f);

        if (const GameObject* owner = Owner(); owner != nullptr)
        {
            const NS::Core::Matrix world = owner->Root().WorldMatrix();
            for (auto& tri : tris)
            {
                tri.v0 = NS::Core::Vector3::Transform(tri.v0, world);
                tri.v1 = NS::Core::Vector3::Transform(tri.v1, world);
                tri.v2 = NS::Core::Vector3::Transform(tri.v2, world);
            }
        }
        return tris;
    }
} // namespace NS::Scene
