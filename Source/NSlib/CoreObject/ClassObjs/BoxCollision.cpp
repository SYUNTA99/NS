#include "NSlib/CoreObject/ClassObjs/BoxCollision.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/OBB.h"

#include "NSlib/Phys/PhysicsScene.h"

#include <cmath>

namespace NS
{
    namespace
    {
        [[nodiscard]] Vector3 ClampNonNegative(const Vector3& v) noexcept
        {
            float x = v.x;
            if (x < 0.0f)
            {
                x = 0.0f;
            }
            float y = v.y;
            if (y < 0.0f)
            {
                y = 0.0f;
            }
            float z = v.z;
            if (z < 0.0f)
            {
                z = 0.0f;
            }
            return Vector3{x, y, z};
        }
    } // namespace

    BoxCollision::BoxCollision(const Vector3& halfExtents) noexcept : m_halfExtents(ClampNonNegative(halfExtents))
    {}

    void BoxCollision::SetHalfExtents(const Vector3& halfExtents) noexcept
    {
        MarkShapeChanged();
        m_halfExtents = ClampNonNegative(halfExtents);
    }

    Vector3 BoxCollision::HalfExtents() const noexcept { return m_halfExtents; }

    void BoxCollision::SetCenterOffset(const Vector3& offset) noexcept
    {
        MarkShapeChanged();
        m_centerOffset = offset;
    }

    Vector3 BoxCollision::CenterOffset() const noexcept { return m_centerOffset; }

    void BoxCollision::SetLocalRotation(const Quaternion& rotation) noexcept
    {
        MarkShapeChanged();
        m_localRotation = rotation;
    }

    Quaternion BoxCollision::LocalRotation() const noexcept { return m_localRotation; }

    void BoxCollision::SetRotationEulerDegrees(const Vector3& eulerDegrees) noexcept
    {
        MarkShapeChanged();
        m_localRotation = EulerDegreesToQuaternion(eulerDegrees);
    }

    Vector3 BoxCollision::RotationEulerDegrees() const noexcept
    {
        return QuaternionToEulerDegrees(m_localRotation);
    }

    AABB BoxCollision::WorldAABB(const Matrix& world) const noexcept
    {
        const Matrix combined = ShapeWorldMatrix(world, m_localRotation, m_centerOffset);
        const AABB local(Vector3{0.0f, 0.0f, 0.0f}, m_halfExtents);
        AABB result;
        local.Transform(result, combined);
        return result;
    }

    OBB BoxCollision::WorldOBB(const Matrix& world) const noexcept
    {
        const AffineDecomposition decomposed =
            DecomposeAffine(ShapeWorldMatrix(world, m_localRotation, m_centerOffset));
        const Vector3 half{m_halfExtents.x * std::abs(decomposed.scale.x),
                               m_halfExtents.y * std::abs(decomposed.scale.y),
                               m_halfExtents.z * std::abs(decomposed.scale.z)};

        return MakeOBB(decomposed.translation, decomposed.rotation, half);
    }

    JPH::BodyID BoxCollision::SyncBody(Phys::PhysicsScene& physics, JPH::BodyID current, const Matrix& world)
    {
        return physics.SyncBox(current, WorldOBB(world), Phys::ObjectLayers::Terrain);
    }
} // namespace NS
