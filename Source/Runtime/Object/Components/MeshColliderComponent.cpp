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
            result.push_back(NS::Physics::Triangle{NS::Core::Vector3::Transform(tri.v0, world),
                                                   NS::Core::Vector3::Transform(tri.v1, world),
                                                   NS::Core::Vector3::Transform(tri.v2, world)});
        }
        return result;
    }

    void MeshColliderComponent::SyncToPhysics(NS::Physics::PhysicsWorld& physics)
    {
        const std::vector<NS::Physics::Triangle> triangles = WorldTriangles();
        TrackBody(physics, physics.SyncMesh(BodyIn(physics), triangles, NS::Physics::ObjectLayers::Terrain));
    }

    // 三角形群はリフレクションで運べないので data からは空で作る。差すのは呼出側の SetLocalTriangles
    NS_CLASS(MeshColliderComponent)
} // namespace NS::Object
