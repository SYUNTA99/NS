#include "NSlib/CoreObject/SubObjects/SphereCollision.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Sphere.h"

#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"
#include "NSlib/Physics/PhysicsScene.h"

#include <algorithm>
#include <cmath>

namespace NS
{
    SphereCollision::SphereCollision(float radius) noexcept : m_radius(std::max(radius, 0.0f)) {}

    void SphereCollision::SetRadius(float radius) noexcept
    {
        m_radius = std::max(radius, 0.0f);
    }

    float SphereCollision::Radius() const noexcept
    {
        return m_radius;
    }

    void SphereCollision::SetCenterOffset(const Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    Vector3 SphereCollision::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    Sphere SphereCollision::WorldSphere() const noexcept
    {
        const SceneActor* owner = Owner();
        if (owner == nullptr)
        {
            return Sphere{m_centerOffset, m_radius};
        }

        const Matrix world = owner->Root().WorldMatrix();
        const Vector3 center = Vector3::Transform(m_centerOffset, world);

        const Vector3 scale = DecomposeAffine(world).scale;
        return Sphere{center, m_radius * MaxAbsComponent(scale)};
    }

    AABB SphereCollision::WorldAABB() const noexcept
    {
        const Sphere s = WorldSphere();
        return AABB{s.center, Vector3{s.radius, s.radius, s.radius}};
    }

    JPH::BodyID SphereCollision::SyncBody(Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        return physics.SyncSphere(current, WorldSphere(), Phys::ObjectLayers::Terrain);
    }

    NS_CLASS(SphereCollision)
} // namespace NS
