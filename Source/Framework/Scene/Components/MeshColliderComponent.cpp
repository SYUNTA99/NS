#include "Framework/Scene/Components/MeshColliderComponent.h"

#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"


namespace NS::Scene
{
    MeshColliderComponent::MeshColliderComponent() noexcept {}

    MeshColliderComponent::MeshColliderComponent(std::vector<NS::Physics::Triangle> localTriangles) noexcept
        : m_localTriangles(std::move(localTriangles))
    {}

    void MeshColliderComponent::SetLocalTriangles(std::vector<NS::Physics::Triangle> localTriangles) noexcept
    {
        m_localTriangles = std::move(localTriangles);
    }

    const std::vector<NS::Physics::Triangle>& MeshColliderComponent::LocalTriangles() const noexcept
    {
        return m_localTriangles;
    }

    std::vector<NS::Physics::Triangle> MeshColliderComponent::WorldTriangles() const
    {
        const GameObject* owner = Owner();
        if (owner == nullptr)
            return m_localTriangles;

        const NS::Math::Matrix world = owner->Root().WorldMatrix();
        std::vector<NS::Physics::Triangle> result;
        result.reserve(m_localTriangles.size());
        for (const NS::Physics::Triangle& tri : m_localTriangles)
        {
            NS::Physics::Triangle worldTri;
            worldTri.v0 = NS::Math::Vector3::Transform(tri.v0, world);
            worldTri.v1 = NS::Math::Vector3::Transform(tri.v1, world);
            worldTri.v2 = NS::Math::Vector3::Transform(tri.v2, world);
            result.push_back(worldTri);
        }
        return result;
    }

    // 三角形群は asset 由来なので data からは空で作り、BuildPlacedObject が読み込み時に差し込む
    NS_REGISTER_COMPONENT(MeshColliderComponent)
} // namespace NS::Scene
