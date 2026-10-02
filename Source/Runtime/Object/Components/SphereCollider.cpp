#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Sphere.h"

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>
#include <cmath>

namespace NS::Obj
{
    SphereCollider::SphereCollider() noexcept {}

    void SphereCollider::SetRadius(float radius) noexcept
    {
        m_radius = std::max(radius, 0.0f);
    }

    float SphereCollider::Radius() const noexcept
    {
        return m_radius;
    }

    void SphereCollider::SetCenterOffset(const NS::Core::Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    NS::Core::Vector3 SphereCollider::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    NS::Core::Sphere SphereCollider::WorldSphere() const noexcept
    {
        const Actor* owner = Owner();
        if (owner == nullptr)
        {
            return NS::Core::Sphere{ m_centerOffset, m_radius };
        }

        const NS::Core::Matrix world = owner->Root().WorldMatrix();
        const NS::Core::Vector3 center = NS::Core::Vector3::Transform(m_centerOffset, world);

        const NS::Core::Vector3 scale = NS::Core::DecomposeAffine(world).scale;
        return NS::Core::Sphere{center, m_radius * NS::Core::MaxAbsComponent(scale)};
    }

    NS::Core::AABB SphereCollider::WorldAABB() const noexcept
    {
        const NS::Core::Sphere s = WorldSphere();
        return NS::Core::AABB{s.center, NS::Core::Vector3{s.radius, s.radius, s.radius}};
    }

    NS::Phys::ShapePart SphereCollider::RigidBodyPart() const
    {
        return NS::Phys::MakeSpherePart(WorldSphere());
    }

    JPH::BodyID SphereCollider::SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        return physics.SyncSphere(current, WorldSphere(), NS::Phys::ObjectLayers::Terrain);
    }

    NS_CLASS(SphereCollider)
} // namespace NS::Obj
