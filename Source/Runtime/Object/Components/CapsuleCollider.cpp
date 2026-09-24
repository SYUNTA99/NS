#include "Runtime/Object/Components/CapsuleCollider.h"
#include "Runtime/Core/AABB.h"

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>
#include <cmath>

namespace NS::Obj
{
    CapsuleCollider::CapsuleCollider() noexcept {}

    CapsuleCollider::CapsuleCollider(float radius, float halfHeight) noexcept
        : m_radius([&]() -> float {
              if (radius < 0.0f)
              {
                 return 0.0f;
              }
              return radius;
          }()),
          m_halfHeight([&]() -> float {
              if (halfHeight < 0.0f)
              {
                    return 0.0f;
              }
              return halfHeight;
          }())
    {}

    void CapsuleCollider::SetRadius(float radius) noexcept
    {
        if (radius < 0.0f)
        {
            m_radius = 0.0f;
        }
        else
        {
            m_radius = radius;
        }
    }

    float CapsuleCollider::Radius() const noexcept
    {
        return m_radius;
    }

    void CapsuleCollider::SetHalfHeight(float halfHeight) noexcept
    {
        if (halfHeight < 0.0f)
        {
            m_halfHeight = 0.0f;
        }
        else
        {
			m_halfHeight = halfHeight;
        }
    }

    float CapsuleCollider::HalfHeight() const noexcept
    {
        return m_halfHeight;
    }

    void CapsuleCollider::SetCenterOffset(const NS::Core::Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    NS::Core::Vector3 CapsuleCollider::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    void CapsuleCollider::SetLocalRotation(const NS::Core::Quaternion& rotation) noexcept
    {
        m_localRotation = rotation;
    }

    NS::Core::Quaternion CapsuleCollider::LocalRotation() const noexcept
    {
        return m_localRotation;
    }

    void CapsuleCollider::SetRotationEulerDegrees(const NS::Core::Vector3& eulerDegrees) noexcept
    {
        m_localRotation = NS::Core::EulerDegreesToQuaternion(eulerDegrees);
    }

    NS::Core::Vector3 CapsuleCollider::RotationEulerDegrees() const noexcept
    {
        return NS::Core::QuaternionToEulerDegrees(m_localRotation);
    }

    NS::Core::Matrix CapsuleCollider::CapsuleWorldMatrix() const noexcept
    {
        const GameObject* owner = Owner();
        const NS::Core::Matrix local = NS::Core::Matrix::CreateFromQuaternion(m_localRotation) *
                                       NS::Core::Matrix::CreateTranslation(m_centerOffset);
        return owner != nullptr ? local * owner->Root().WorldMatrix() : local;
    }

    NS::Phys::Capsule CapsuleCollider::WorldCapsule() const noexcept
    {
        const NS::Core::AffineDecomposition decomposed = NS::Core::DecomposeAffine(CapsuleWorldMatrix());
        const NS::Core::Vector3& scale = decomposed.scale;
        const NS::Core::Quaternion& rotation = decomposed.rotation;
        const NS::Core::Vector3& translation = decomposed.translation;
        return NS::Phys::Capsule{translation,
                                    NS::Core::Vector3::Transform(NS::Core::Vector3::UnitY, rotation),
                                    m_halfHeight * std::abs(scale.y),
                                    m_radius * std::max(std::abs(scale.x), std::abs(scale.z))};
    }

    NS::Core::AABB CapsuleCollider::WorldAABB() const noexcept
    {
        const NS::Phys::Capsule capsule = WorldCapsule();
        const NS::Core::Vector3 tip = capsule.center + capsule.axis * capsule.halfHeight;
        const NS::Core::Vector3 base = capsule.center - capsule.axis * capsule.halfHeight;
        const NS::Core::Vector3 r{capsule.radius, capsule.radius, capsule.radius};
        const NS::Core::Vector3 lo = NS::Core::Vector3::Min(tip, base) - r;
        const NS::Core::Vector3 hi = NS::Core::Vector3::Max(tip, base) + r;
        return NS::Core::AABB{(lo + hi) * 0.5f, (hi - lo) * 0.5f};
    }

    void CapsuleCollider::SetExcludedFromStaticWorld(bool excluded) noexcept
    {
        m_excludedFromStaticWorld = excluded;
    }

    NS::Phys::ShapePart CapsuleCollider::RigidBodyPart() const
    {
        return NS::Phys::MakeCapsulePart(WorldCapsule());
    }

    JPH::BodyID CapsuleCollider::SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        if (m_excludedFromStaticWorld)
        {
            return JPH::BodyID{};
        }
        return physics.SyncCapsule(current, WorldCapsule(), NS::Phys::ObjectLayers::Terrain);
    }

    NS_CLASS(CapsuleCollider)
} // namespace NS::Obj
