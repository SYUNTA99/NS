#include "Framework/Scene/Components/CapsuleColliderComponent.h"

#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

#include <cmath>

namespace NS::Scene
{
    CapsuleColliderComponent::CapsuleColliderComponent() noexcept {}

    CapsuleColliderComponent::CapsuleColliderComponent(float radius, float halfHeight) noexcept
        : m_radius(radius < 0.0f ? 0.0f : radius), m_halfHeight(halfHeight < 0.0f ? 0.0f : halfHeight)
    {}

    void CapsuleColliderComponent::SetRadius(float radius) noexcept
    {
        m_radius = radius < 0.0f ? 0.0f : radius;
    }

    float CapsuleColliderComponent::Radius() const noexcept
    {
        return m_radius;
    }

    void CapsuleColliderComponent::SetHalfHeight(float halfHeight) noexcept
    {
        m_halfHeight = halfHeight < 0.0f ? 0.0f : halfHeight;
    }

    float CapsuleColliderComponent::HalfHeight() const noexcept
    {
        return m_halfHeight;
    }

    void CapsuleColliderComponent::SetCenterOffset(const NS::Math::Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    NS::Math::Vector3 CapsuleColliderComponent::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    void CapsuleColliderComponent::SetLocalRotation(const NS::Math::Quaternion& rotation) noexcept
    {
        m_localRotation = rotation;
    }

    NS::Math::Quaternion CapsuleColliderComponent::LocalRotation() const noexcept
    {
        return m_localRotation;
    }

    void CapsuleColliderComponent::SetRotationEulerDegrees(const NS::Math::Vector3& eulerDegrees) noexcept
    {
        m_localRotation =
            NS::Math::Quaternion::CreateFromYawPitchRoll(NS::Math::Vector3{NS::Math::DegreesToRadians(eulerDegrees.x),
                                                                           NS::Math::DegreesToRadians(eulerDegrees.y),
                                                                           NS::Math::DegreesToRadians(eulerDegrees.z)});
    }

    NS::Math::Vector3 CapsuleColliderComponent::RotationEulerDegrees() const noexcept
    {
        const NS::Math::Vector3 euler = m_localRotation.ToEuler();
        return NS::Math::Vector3{NS::Math::RadiansToDegrees(euler.x),
                                 NS::Math::RadiansToDegrees(euler.y),
                                 NS::Math::RadiansToDegrees(euler.z)};
    }

    NS::Physics::Capsule CapsuleColliderComponent::WorldCapsule() const noexcept
    {
        const GameObject* owner = Owner();
        const NS::Math::Matrix local = NS::Math::Matrix::CreateFromQuaternion(m_localRotation) *
                                       NS::Math::Matrix::CreateTranslation(m_centerOffset);
        NS::Math::Matrix combined = (owner != nullptr) ? local * owner->Root().WorldMatrix() : local;

        NS::Math::Vector3 scale{1.0f, 1.0f, 1.0f};
        NS::Math::Quaternion rotation = NS::Math::Quaternion::Identity;
        NS::Math::Vector3 translation{0.0f, 0.0f, 0.0f};
        combined.Decompose(scale, rotation, translation);

        const float radiusScale = std::abs(scale.x) > std::abs(scale.z) ? std::abs(scale.x) : std::abs(scale.z);

        NS::Physics::Capsule capsule;
        capsule.center = translation;
        capsule.axis = NS::Math::Vector3::Transform(NS::Math::Vector3::UnitY, rotation);
        capsule.radius = m_radius * radiusScale;
        capsule.halfHeight = m_halfHeight * std::abs(scale.y);
        return capsule;
    }

    NS::Math::AABB CapsuleColliderComponent::WorldAABB() const noexcept
    {
        const NS::Physics::Capsule c = WorldCapsule();
        const NS::Math::Vector3 tip = c.center + c.axis * c.halfHeight;
        const NS::Math::Vector3 base = c.center - c.axis * c.halfHeight;
        const NS::Math::Vector3 r{c.radius, c.radius, c.radius};
        const NS::Math::Vector3 lo = NS::Math::Vector3::Min(tip, base) - r;
        const NS::Math::Vector3 hi = NS::Math::Vector3::Max(tip, base) + r;
        return NS::Math::AABB{(lo + hi) * 0.5f, (hi - lo) * 0.5f};
    }

    NS_REGISTER_COMPONENT(CapsuleColliderComponent)
} // namespace NS::Scene
