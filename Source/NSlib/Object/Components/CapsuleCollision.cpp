#include "NSlib/Object/Components/CapsuleCollision.h"
#include "NSlib/Core/AABB.h"

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Transform.h"
#include "NSlib/Physics/PhysicsScene.h"

#include <algorithm>
#include <cmath>

namespace NS::Obj
{
    CapsuleCollision::CapsuleCollision(float radius, float halfHeight) noexcept
    {
        SetRadius(radius);
        SetHalfHeight(halfHeight);
    }

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

    void CapsuleCollision::SetCenterOffset(const NS::Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    NS::Vector3 CapsuleCollision::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    void CapsuleCollision::SetLocalRotation(const NS::Quaternion& rotation) noexcept
    {
        m_localRotation = rotation;
    }

    NS::Quaternion CapsuleCollision::LocalRotation() const noexcept
    {
        return m_localRotation;
    }

    void CapsuleCollision::SetRotationEulerDegrees(const NS::Vector3& eulerDegrees) noexcept
    {
        m_localRotation = NS::EulerDegreesToQuaternion(eulerDegrees);
    }

    NS::Vector3 CapsuleCollision::RotationEulerDegrees() const noexcept
    {
        return NS::QuaternionToEulerDegrees(m_localRotation);
    }

    NS::Phys::Capsule CapsuleCollision::WorldCapsule() const noexcept
    {
        const NS::AffineDecomposition decomposed =
            NS::DecomposeAffine(ShapeWorldMatrix(m_localRotation, m_centerOffset));
        const NS::Vector3& scale = decomposed.scale;
        const NS::Quaternion& rotation = decomposed.rotation;
        const NS::Vector3& translation = decomposed.translation;
        return NS::Phys::Capsule{translation,
                                 NS::Vector3::Transform(NS::Vector3::UnitY, rotation),
                                 m_halfHeight * std::abs(scale.y),
                                 m_radius * std::max(std::abs(scale.x), std::abs(scale.z))};
    }

    NS::AABB CapsuleCollision::WorldAABB() const noexcept
    {
        const NS::Phys::Capsule capsule = WorldCapsule();
        const NS::Vector3 tip = capsule.center + capsule.axis * capsule.halfHeight;
        const NS::Vector3 base = capsule.center - capsule.axis * capsule.halfHeight;
        const NS::Vector3 r{capsule.radius, capsule.radius, capsule.radius};
        const NS::Vector3 lo = NS::Vector3::Min(tip, base) - r;
        const NS::Vector3 hi = NS::Vector3::Max(tip, base) + r;
        return NS::AABB{(lo + hi) * 0.5f, (hi - lo) * 0.5f};
    }

    JPH::BodyID CapsuleCollision::SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        return physics.SyncCapsule(current, WorldCapsule(), NS::Phys::ObjectLayers::Terrain);
    }

    NS_CLASS(CapsuleCollision)
} // namespace NS::Obj
