#include "Framework/Scene/Components/SphereColliderComponent.h"

#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

#include <algorithm>
#include <cmath>

namespace NS::Scene
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

    NS::Physics::Sphere SphereColliderComponent::WorldSphere() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner == nullptr)
            return NS::Physics::Sphere{m_centerOffset, m_radius};

        NS::Math::Matrix world = owner->Root().WorldMatrix();
        const NS::Math::Vector3 center = NS::Math::Vector3::Transform(m_centerOffset, world);

        NS::Math::Vector3 scale{1.0f, 1.0f, 1.0f};
        NS::Math::Quaternion rotation = NS::Math::Quaternion::Identity;
        NS::Math::Vector3 translation{0.0f, 0.0f, 0.0f};
        world.Decompose(scale, rotation, translation);

        return NS::Physics::Sphere{center, m_radius * MaxAbsComponent(scale)};
    }

    NS::Math::AABB SphereColliderComponent::WorldAABB() const noexcept
    {
        const NS::Physics::Sphere s = WorldSphere();
        return NS::Math::AABB{s.center, NS::Math::Vector3{s.radius, s.radius, s.radius}};
    }

    NS_REGISTER_COMPONENT(SphereColliderComponent)
} // namespace NS::Scene
