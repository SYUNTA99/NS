#include "Runtime/Object/Components/SphereColliderComponent.h"

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>

namespace NS::Object
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

    SphereColliderComponent::SphereColliderComponent() noexcept {}

    SphereColliderComponent::SphereColliderComponent(float radius) noexcept : m_radius(std::max(radius, 0.0f)) {}

    void SphereColliderComponent::SetRadius(float radius) noexcept
    {
        m_radius = std::max(radius, 0.0f);
    }

    float SphereColliderComponent::Radius() const noexcept
    {
        return m_radius;
    }

    void SphereColliderComponent::SetCenterOffset(const NS::Core::Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    NS::Core::Vector3 SphereColliderComponent::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    NS::Core::Sphere SphereColliderComponent::WorldSphere() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr)
            return NS::Core::Sphere{m_centerOffset, m_radius};

        const NS::Core::Matrix world = owner->Root().WorldMatrix();
        const NS::Core::Vector3 center = NS::Core::Vector3::Transform(m_centerOffset, world);

        const NS::Core::Vector3 scale = NS::Core::DecomposeAffine(world).scale;
        return NS::Core::Sphere{center, m_radius * MaxAbsComponent(scale)};
    }

    NS::Core::AABB SphereColliderComponent::WorldAABB() const noexcept
    {
        const NS::Core::Sphere s = WorldSphere();
        return NS::Core::AABB{s.center, NS::Core::Vector3{s.radius, s.radius, s.radius}};
    }

    void SphereColliderComponent::AddToPhysics(NS::Physics::PhysicsWorld& physics) const
    {
        physics.AddSphere(WorldSphere());
    }

    NS_CLASS(SphereColliderComponent)
} // namespace NS::Object
