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
        [[nodiscard]] float MaxAbsComponent(const NS::Math::Vector3& v) noexcept
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

    void SphereColliderComponent::SetCenterOffset(const NS::Math::Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    NS::Math::Vector3 SphereColliderComponent::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    NS::Math::Sphere SphereColliderComponent::WorldSphere() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr)
            return NS::Math::Sphere{m_centerOffset, m_radius};

        const NS::Math::Matrix world = owner->Root().WorldMatrix();
        const NS::Math::Vector3 center = NS::Math::Vector3::Transform(m_centerOffset, world);

        const NS::Math::Vector3 scale = NS::Math::DecomposeAffine(world).scale;
        return NS::Math::Sphere{center, m_radius * MaxAbsComponent(scale)};
    }

    NS::Math::AABB SphereColliderComponent::WorldAABB() const noexcept
    {
        const NS::Math::Sphere s = WorldSphere();
        return NS::Math::AABB{s.center, NS::Math::Vector3{s.radius, s.radius, s.radius}};
    }

    void SphereColliderComponent::AddToPhysics(NS::Physics::PhysicsWorld& physics) const
    {
        physics.AddSphere(WorldSphere());
    }

    NS_CLASS(SphereColliderComponent)
} // namespace NS::Object
