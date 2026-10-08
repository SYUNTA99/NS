#include "NSlib/Object/Components/BoxCollision.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/OBB.h"

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Transform.h"
#include "NSlib/Physics/PhysicsScene.h"

#include <cmath>

namespace NS::Obj
{
    namespace
    {
        [[nodiscard]] NS::Vector3 ClampNonNegative(const NS::Vector3& v) noexcept
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
            return NS::Vector3{x, y, z};
        }
    } // namespace

    BoxCollision::BoxCollision(const NS::Vector3& halfExtents) noexcept : m_halfExtents(ClampNonNegative(halfExtents))
    {}

    void BoxCollision::SetHalfExtents(const NS::Vector3& halfExtents) noexcept
    {
        m_halfExtents = ClampNonNegative(halfExtents);
    }

    NS::Vector3 BoxCollision::HalfExtents() const noexcept
    {
        return m_halfExtents;
    }

    void BoxCollision::SetCenterOffset(const NS::Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    NS::Vector3 BoxCollision::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    void BoxCollision::SetLocalRotation(const NS::Quaternion& rotation) noexcept
    {
        m_localRotation = rotation;
    }

    NS::Quaternion BoxCollision::LocalRotation() const noexcept
    {
        return m_localRotation;
    }

    void BoxCollision::SetRotationEulerDegrees(const NS::Vector3& eulerDegrees) noexcept
    {
        m_localRotation = NS::EulerDegreesToQuaternion(eulerDegrees);
    }

    NS::Vector3 BoxCollision::RotationEulerDegrees() const noexcept
    {
        return NS::QuaternionToEulerDegrees(m_localRotation);
    }

    NS::AABB BoxCollision::WorldAABB() const noexcept
    {
        // 原点中心 + 半径の local box に、当たり箱の local offset / 回転 → owner の world 変換の順で重ねる
        // 回転時は内包する軸並行 AABB になる
        const NS::Matrix combined = ShapeWorldMatrix(m_localRotation, m_centerOffset);
        const NS::AABB local(NS::Vector3{0.0f, 0.0f, 0.0f}, m_halfExtents);
        NS::AABB world;
        local.Transform(world, combined);
        return world;
    }

    NS::OBB BoxCollision::WorldOBB() const noexcept
    {
        const NS::AffineDecomposition decomposed =
            NS::DecomposeAffine(ShapeWorldMatrix(m_localRotation, m_centerOffset));
        const NS::Vector3 half{m_halfExtents.x * std::abs(decomposed.scale.x),
                               m_halfExtents.y * std::abs(decomposed.scale.y),
                               m_halfExtents.z * std::abs(decomposed.scale.z)};

        return NS::MakeOBB(decomposed.translation, decomposed.rotation, half);
    }

    JPH::BodyID BoxCollision::SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        return physics.SyncBox(current, WorldOBB(), NS::Phys::ObjectLayers::Terrain);
    }

    NS_CLASS(BoxCollision)
} // namespace NS::Obj
