#include "NSlib/CoreObject/ClassObjs/CapsuleCollision.h"
#include "NSlib/Core/AABB.h"

#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Phys/PhysicsScene.h"

namespace NS
{
    CapsuleCollision::CapsuleCollision(SceneActor* owner, float radius, float halfHeight) noexcept : Collision(owner)
    {
        SetRadius(radius);
        SetHalfHeight(halfHeight);
    }

    void CapsuleCollision::SetRadius(float radius) noexcept
    {
        MarkShapeChanged();
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
        MarkShapeChanged();
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

    void CapsuleCollision::SetCenterOffset(const Vector3& offset) noexcept
    {
        MarkShapeChanged();
        m_centerOffset = offset;
    }

    Vector3 CapsuleCollision::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    void CapsuleCollision::SetLocalRotation(const Quaternion& rotation) noexcept
    {
        MarkShapeChanged();
        m_localRotation = rotation;
    }

    Quaternion CapsuleCollision::LocalRotation() const noexcept
    {
        return m_localRotation;
    }

    void CapsuleCollision::SetRotationEulerDegrees(const Vector3& eulerDegrees) noexcept
    {
        MarkShapeChanged();
        m_localRotation = EulerDegreesToQuaternion(eulerDegrees);
    }

    Vector3 CapsuleCollision::RotationEulerDegrees() const noexcept
    {
        return QuaternionToEulerDegrees(m_localRotation);
    }

    Phys::Capsule CapsuleCollision::WorldCapsule() const noexcept
    {
        const AffineDecomposition decomposed =
            DecomposeAffine(ShapeWorldMatrix(m_localRotation, m_centerOffset));
        return Phys::MakeScaledCapsule(
            decomposed.translation, decomposed.rotation, decomposed.scale, m_radius, m_halfHeight);
    }

    AABB CapsuleCollision::WorldAABB() const noexcept
    {
        const Phys::Capsule capsule = WorldCapsule();
        const Vector3 tip = capsule.center + capsule.axis * capsule.halfHeight;
        const Vector3 base = capsule.center - capsule.axis * capsule.halfHeight;
        const Vector3 r{capsule.radius, capsule.radius, capsule.radius};
        const Vector3 lo = Vector3::Min(tip, base) - r;
        const Vector3 hi = Vector3::Max(tip, base) + r;
        return AABB{(lo + hi) * 0.5f, (hi - lo) * 0.5f};
    }

    JPH::BodyID CapsuleCollision::SyncBody(Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        return physics.SyncCapsule(current, WorldCapsule(), Phys::ObjectLayers::Terrain);
    }
} // namespace NS
