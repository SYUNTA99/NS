#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Sphere.h"

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>
#include <cmath>

namespace NS::Obj
{
    namespace
    {
        [[nodiscard]] float MaxAbsComponent(const NS::Core::Vector3& v) noexcept
        {
            const float ax = std::abs(v.x);
            const float ay = std::abs(v.y);
            const float az = std::abs(v.z);
            return std::max(ax, std::max(ay, az));
        }
    } // namespace

    SphereCollider::SphereCollider() noexcept {}

    SphereCollider::SphereCollider(float radius) noexcept : m_radius(std::max(radius, 0.0f)) {}

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
        const GameObject* owner = Owner();
        if (owner == nullptr)
        {
            return NS::Core::Sphere{ m_centerOffset, m_radius };
        }

        const NS::Core::Matrix world = owner->Root().WorldMatrix();
        const NS::Core::Vector3 center = NS::Core::Vector3::Transform(m_centerOffset, world);

        const NS::Core::Vector3 scale = NS::Core::DecomposeAffine(world).scale;
        return NS::Core::Sphere{center, m_radius * MaxAbsComponent(scale)};
    }

    NS::Core::AABB SphereCollider::WorldAABB() const noexcept
    {
        const NS::Core::Sphere s = WorldSphere();
        return NS::Core::AABB{s.center, NS::Core::Vector3{s.radius, s.radius, s.radius}};
    }

    JPH::BodyID SphereCollider::SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        return physics.SyncSphere(current, WorldSphere(), NS::Phys::ObjectLayers::Terrain);
    }

    NS_CLASS(SphereCollider)
} // namespace NS::Obj
