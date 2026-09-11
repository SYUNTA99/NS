#include "Runtime/Object/Components/MeshColliderComponent.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/Components/MeshRendererComponent.h"
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
        // TODO: 張り直しのたびに world 三角形から形を組み直す
        // 大きな地形で張り直しが重くなったら、 形を local で 1 度だけ組んで変換は body 側に持たせる
        const std::vector<NS::Physics::Triangle> triangles = WorldTriangles();
        TrackBody(physics, physics.SyncMesh(BodyIn(physics), triangles, NS::Physics::ObjectLayers::Terrain));
    }

    void MeshColliderComponent::ResolveAssets(AssetManager& assets)
    {
        const GameObject* owner = Owner();
        if (owner == nullptr)
            return;
        const MeshRendererComponent* renderer = owner->FindComponent<MeshRendererComponent>();
        if (renderer == nullptr)
        {
            NS_LOG_WARN(Scene, "MeshColliderComponent: 同じ object に MeshRendererComponent が無く、 当たりは空のまま");
            return;
        }

        const std::vector<NS::Physics::Triangle>* triangles = assets.GetOrLoadMeshCollision(renderer->MeshRef());
        if (triangles == nullptr)
            triangles = assets.GetOrLoadMeshCollision("cube");
        if (triangles == nullptr)
            return;
        SetLocalTriangles(*triangles);
    }

    // data からは空で作る。 三角形は ResolveAssets が描画の参照から入れる
    NS_CLASS(MeshColliderComponent)
} // namespace NS::Object
