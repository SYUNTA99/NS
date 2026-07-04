#include "Framework/Scene/Components/SlopeColliderComponent.h"

#include "Framework/Physics/WedgeGeometry.h"
#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

namespace NS::Scene
{
    SlopeColliderComponent::SlopeColliderComponent(float angleDegrees, const NS::Math::Vector3& halfExtents) noexcept
        : m_angleDegrees(angleDegrees), m_halfExtents(halfExtents)
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
        // local 生成では rotation を Owner transform に載せるので yaw=0。 向き・位置は world matrix で反映
        auto tris = NS::Physics::BuildWedgeTriangles({0.0f, 0.0f, 0.0f}, m_halfExtents, m_angleDegrees, 0.0f);

        if (const GameObject* owner = Owner(); owner != nullptr)
        {
            const NS::Math::Matrix world = owner->Root().WorldMatrix();
            for (auto& tri : tris)
            {
                tri.v0 = NS::Math::Vector3::Transform(tri.v0, world);
                tri.v1 = NS::Math::Vector3::Transform(tri.v1, world);
                tri.v2 = NS::Math::Vector3::Transform(tri.v2, world);
            }
        }
        return tris;
    }

    // 既定コンストラクタが無いので、読み込み時に反射 / BuildPlacedObject が上書きする placeholder 既定値で構築する
    NS_REGISTER_COMPONENT(SlopeColliderComponent, 45.0f, NS::Math::Vector3{0.5f, 0.5f, 0.5f})
} // namespace NS::Scene
