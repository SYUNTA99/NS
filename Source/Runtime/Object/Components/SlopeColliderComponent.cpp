#include "Runtime/Object/Components/SlopeColliderComponent.h"

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsWorld.h"
#include "Runtime/Physics/WedgeGeometry.h"

namespace NS::Object
{
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

    void SlopeColliderComponent::AddToPhysics(NS::Physics::PhysicsWorld& physics) const
    {
        for (const NS::Physics::Triangle& tri : WorldTriangles())
            physics.AddTriangle(tri);
    }

    NS_CLASS(SlopeColliderComponent)
} // namespace NS::Object
