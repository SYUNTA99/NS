#include "NSlib/CoreObject/ClassObjs/BoxCollision.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/OBB.h"

#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/SceneActor.h"
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

    BoxCollision::BoxCollision(SceneActor* owner, const Vector3& halfExtents) noexcept
        : Collision(owner), m_halfExtents(ClampNonNegative(halfExtents))
    {}

    void BoxCollision::SetHalfExtents(const Vector3& halfExtents) noexcept
    {
        m_halfExtents = ClampNonNegative(halfExtents);
    }

    Vector3 BoxCollision::HalfExtents() const noexcept
    {
        return m_halfExtents;
    }

    void BoxCollision::SetCenterOffset(const Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    Vector3 BoxCollision::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    void BoxCollision::SetLocalRotation(const Quaternion& rotation) noexcept
    {
        m_localRotation = rotation;
    }

    Quaternion BoxCollision::LocalRotation() const noexcept
    {
        return m_localRotation;
    }

    void BoxCollision::SetRotationEulerDegrees(const Vector3& eulerDegrees) noexcept
    {
        m_localRotation = EulerDegreesToQuaternion(eulerDegrees);
    }

    Vector3 BoxCollision::RotationEulerDegrees() const noexcept
    {
        return QuaternionToEulerDegrees(m_localRotation);
    }

    AABB BoxCollision::WorldAABB() const noexcept
    {
        // 原点中心の箱に局所のずれと回転を掛け、その後で持ち主の世界変換を掛ける
        // 回転時は内包する軸並行 AABB になる
        const Matrix combined = ShapeWorldMatrix(m_localRotation, m_centerOffset);
        const AABB local(Vector3{0.0f, 0.0f, 0.0f}, m_halfExtents);
        AABB world;
        local.Transform(world, combined);
        return world;
    }

    OBB BoxCollision::WorldOBB() const noexcept
    {
        const AffineDecomposition decomposed =
            DecomposeAffine(ShapeWorldMatrix(m_localRotation, m_centerOffset));
        const Vector3 half{m_halfExtents.x * std::abs(decomposed.scale.x),
                               m_halfExtents.y * std::abs(decomposed.scale.y),
                               m_halfExtents.z * std::abs(decomposed.scale.z)};

        return MakeOBB(decomposed.translation, decomposed.rotation, half);
    }

    JPH::BodyID BoxCollision::SyncBody(Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        return physics.SyncBox(current, WorldOBB(), Phys::ObjectLayers::Terrain);
    }
} // namespace NS
