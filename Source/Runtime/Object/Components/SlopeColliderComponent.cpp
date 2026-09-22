#include "Runtime/Object/Components/SlopeColliderComponent.h"

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsScene.h"
#include "Runtime/Physics/WedgeGeometry.h"

namespace NS::Obj
{
    std::array<NS::Phys::Triangle, 8> SlopeColliderComponent::WorldTriangles() const noexcept
    {
        // local 生成では rotation を Owner transform に載せるので yaw=0。向き・位置は world matrix で反映
        auto tris = NS::Phys::BuildWedgeTriangles({0.0f, 0.0f, 0.0f}, m_halfExtents, m_angleDegrees, 0.0f);

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

    JPH::BodyID SlopeColliderComponent::SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        const std::array<NS::Phys::Triangle, 8> triangles = WorldTriangles();
        return physics.SyncMesh(current, triangles, NS::Phys::ObjectLayers::Terrain);
    }

    NS_CLASS(SlopeColliderComponent)
} // namespace NS::Obj
