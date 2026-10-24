#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Sphere.h"

#include "NSlib/Phys/PhysicsScene.h"

#include <algorithm>
#include <cmath>

namespace NS
{
    SphereCollision::SphereCollision(float radius) noexcept : m_radius(std::max(radius, 0.0f)) {}

    void SphereCollision::SetRadius(float radius) noexcept
    {
        MarkShapeChanged();
        m_radius = std::max(radius, 0.0f);
    }

    float SphereCollision::Radius() const noexcept
    {
        return m_radius;
    }

    void SphereCollision::SetCenterOffset(const Vector3& offset) noexcept
    {
        MarkShapeChanged();
        m_centerOffset = offset;
    }

    Vector3 SphereCollision::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    Sphere SphereCollision::WorldSphere(const Matrix& world) const noexcept
    {
        const Vector3 center = Vector3::Transform(m_centerOffset, world);

        const Vector3 scale = DecomposeAffine(world).scale;
        return Sphere{center, m_radius * MaxAbsComponent(scale)};
    }

    AABB SphereCollision::WorldAABB(const Matrix& world) const noexcept
    {
        const Sphere s = WorldSphere(world);
        return AABB{s.center, Vector3{s.radius, s.radius, s.radius}};
    }

    JPH::BodyID SphereCollision::SyncBody(Phys::PhysicsScene& physics, JPH::BodyID current, const Matrix& world)
    {
        return physics.SyncSphere(current, WorldSphere(world), Phys::ObjectLayers::Terrain);
    }
} // namespace NS
