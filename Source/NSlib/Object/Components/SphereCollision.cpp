#include "NSlib/Object/Components/SphereCollision.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Sphere.h"

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Transform.h"
#include "NSlib/Physics/PhysicsScene.h"

#include <algorithm>
#include <cmath>

namespace NS::Obj
{
    SphereCollision::SphereCollision() noexcept {}

    SphereCollision::SphereCollision(float radius) noexcept : m_radius(std::max(radius, 0.0f)) {}

    void SphereCollision::SetRadius(float radius) noexcept
    {
        m_radius = std::max(radius, 0.0f);
    }

    float SphereCollision::Radius() const noexcept
    {
        return m_radius;
    }

    void SphereCollision::SetCenterOffset(const NS::Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    NS::Vector3 SphereCollision::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    NS::Sphere SphereCollision::WorldSphere() const noexcept
    {
        const Actor* owner = Owner();
        if (owner == nullptr)
        {
            return NS::Sphere{ m_centerOffset, m_radius };
        }

        const NS::Matrix world = owner->Root().WorldMatrix();
        const NS::Vector3 center = NS::Vector3::Transform(m_centerOffset, world);

        const NS::Vector3 scale = NS::DecomposeAffine(world).scale;
        return NS::Sphere{center, m_radius * NS::MaxAbsComponent(scale)};
    }

    NS::AABB SphereCollision::WorldAABB() const noexcept
    {
        const NS::Sphere s = WorldSphere();
        return NS::AABB{s.center, NS::Vector3{s.radius, s.radius, s.radius}};
    }

    JPH::BodyID SphereCollision::SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        return physics.SyncSphere(current, WorldSphere(), NS::Phys::ObjectLayers::Terrain);
    }

    NS_CLASS(SphereCollision)
} // namespace NS::Obj
