#include "Runtime/Object/Components/CapsuleColliderComponent.h"

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>

namespace NS::Object
{
    CapsuleColliderComponent::CapsuleColliderComponent() noexcept {}

    CapsuleColliderComponent::CapsuleColliderComponent(float radius, float halfHeight) noexcept
        : m_radius([&]() -> float {
              if (radius < 0.0f)
                  return 0.0f;
              return radius;
          }()),
          m_halfHeight([&]() -> float {
              if (halfHeight < 0.0f)
                  return 0.0f;
              return halfHeight;
          }())
    {}

    void CapsuleColliderComponent::SetRadius(float radius) noexcept
    {
        if (radius < 0.0f)
            m_radius = 0.0f;
        else
            m_radius = radius;
    }

    float CapsuleColliderComponent::Radius() const noexcept
    {
        return m_radius;
    }

    void CapsuleColliderComponent::SetHalfHeight(float halfHeight) noexcept
    {
        if (halfHeight < 0.0f)
            m_halfHeight = 0.0f;
        else
            m_halfHeight = halfHeight;
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
        m_localRotation = NS::Math::EulerDegreesToQuaternion(eulerDegrees);
    }

    NS::Math::Vector3 CapsuleColliderComponent::RotationEulerDegrees() const noexcept
    {
        return NS::Math::QuaternionToEulerDegrees(m_localRotation);
    }

    NS::Physics::Capsule CapsuleColliderComponent::WorldCapsule() const noexcept
    {
        const GameObject* owner = Owner();
        const NS::Math::Matrix local = NS::Math::Matrix::CreateFromQuaternion(m_localRotation) *
                                       NS::Math::Matrix::CreateTranslation(m_centerOffset);
        const NS::Math::Matrix combined = [&]() -> NS::Math::Matrix {
            if (owner != nullptr)
                return local * owner->Root().WorldMatrix();
            return local;
        }();

        const auto [scale, rotation, translation] = NS::Math::DecomposeAffine(combined);
        const float radiusScale = std::max(std::abs(scale.x), std::abs(scale.z));

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

    void CapsuleColliderComponent::AddToPhysics(NS::Physics::PhysicsWorld& physics) const
    {
        physics.AddCapsule(WorldCapsule());
    }

    NS_CLASS(CapsuleColliderComponent)
} // namespace NS::Object
