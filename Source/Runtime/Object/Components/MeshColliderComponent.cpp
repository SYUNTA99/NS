#include "Runtime/Object/Components/MeshColliderComponent.h"

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsWorld.h"

namespace NS::Object
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

        const NS::Core::Matrix world = owner->Root().WorldMatrix();
        std::vector<NS::Physics::Triangle> result;
        result.reserve(m_localTriangles.size());
        for (const NS::Physics::Triangle& tri : m_localTriangles)
        {
            NS::Physics::Triangle worldTri;
            worldTri.v0 = NS::Core::Vector3::Transform(tri.v0, world);
            worldTri.v1 = NS::Core::Vector3::Transform(tri.v1, world);
            worldTri.v2 = NS::Core::Vector3::Transform(tri.v2, world);
            result.push_back(worldTri);
        }
        return result;
    }

    void MeshColliderComponent::AddToPhysics(NS::Physics::PhysicsWorld& physics) const
    {
        for (const NS::Physics::Triangle& tri : WorldTriangles())
            physics.AddTriangle(tri);
    }

    // 三角形群は asset 由来なので data からは空で作り、読み込み時に ResolveAssets が差し込む
    NS_CLASS(MeshColliderComponent)
} // namespace NS::Object
