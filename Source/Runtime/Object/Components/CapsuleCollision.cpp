#include "Runtime/Object/Components/CapsuleCollision.h"
#include "Runtime/Core/AABB.h"

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>
#include <cmath>

namespace NS::Obj
{
    CapsuleCollision::CapsuleCollision() noexcept {}

    CapsuleCollision::CapsuleCollision(float radius, float halfHeight) noexcept
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

    void CapsuleCollision::SetRadius(float radius) noexcept
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

    float CapsuleCollision::Radius() const noexcept
    {
        return m_radius;
    }

    void CapsuleCollision::SetHalfHeight(float halfHeight) noexcept
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

    float CapsuleCollision::HalfHeight() const noexcept
    {
        return m_halfHeight;
    }

    void CapsuleCollision::SetCenterOffset(const NS::Core::Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    NS::Core::Vector3 CapsuleCollision::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    void CapsuleCollision::SetLocalRotation(const NS::Core::Quaternion& rotation) noexcept
    {
        m_localRotation = rotation;
    }

    NS::Core::Quaternion CapsuleCollision::LocalRotation() const noexcept
    {
        return m_localRotation;
    }

    void CapsuleCollision::SetRotationEulerDegrees(const NS::Core::Vector3& eulerDegrees) noexcept
    {
        m_localRotation = NS::Core::EulerDegreesToQuaternion(eulerDegrees);
    }

    NS::Core::Vector3 CapsuleCollision::RotationEulerDegrees() const noexcept
    {
        return NS::Core::QuaternionToEulerDegrees(m_localRotation);
    }

    NS::Core::Matrix CapsuleCollision::CapsuleWorldMatrix() const noexcept
    {
        const Actor* owner = Owner();
        const NS::Core::Matrix local = NS::Core::Matrix::CreateFromQuaternion(m_localRotation) *
                                       NS::Core::Matrix::CreateTranslation(m_centerOffset);
        if (owner == nullptr)
        {
            return local;
        }
        return local * owner->Root().WorldMatrix();
    }

    NS::Phys::Capsule CapsuleCollision::WorldCapsule() const noexcept
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

    NS::Core::AABB CapsuleCollision::WorldAABB() const noexcept
    {
        const NS::Phys::Capsule capsule = WorldCapsule();
        const NS::Core::Vector3 tip = capsule.center + capsule.axis * capsule.halfHeight;
        const NS::Core::Vector3 base = capsule.center - capsule.axis * capsule.halfHeight;
        const NS::Core::Vector3 r{capsule.radius, capsule.radius, capsule.radius};
        const NS::Core::Vector3 lo = NS::Core::Vector3::Min(tip, base) - r;
        const NS::Core::Vector3 hi = NS::Core::Vector3::Max(tip, base) + r;
        return NS::Core::AABB{(lo + hi) * 0.5f, (hi - lo) * 0.5f};
    }

    JPH::BodyID CapsuleCollision::SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        return physics.SyncCapsule(current, WorldCapsule(), NS::Phys::ObjectLayers::Terrain);
    }

    NS_CLASS(CapsuleCollision)
} // namespace NS::Obj
