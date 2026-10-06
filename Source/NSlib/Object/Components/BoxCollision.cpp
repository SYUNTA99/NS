#include "NSlib/Object/Components/BoxCollision.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/OBB.h"

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Transform.h"
#include "NSlib/Physics/PhysicsScene.h"

#include <algorithm>
#include <cmath>

namespace NS::Obj
{
    namespace
    {
        // OBB の 3 軸が座標軸に十分沿っていれば軸並行とみなす。90° 刻みの回転はここに落ちる
        // 各軸は単位ベクトルなので最大成分が 1 に届けば残り 2 成分はほぼ 0 になる
        // しきい 1e-4 は 90° を quaternion 経由で組んだ時の float 誤差を確実に飲み込み、1° 以上の傾きは OBB へ回す
        [[nodiscard]] bool IsAxisAligned(const NS::OBB& obb) noexcept
        {
            constexpr float k_AlignEpsilon = 1e-4f;
            bool (*const alignedAxis)(const NS::Vector3&) noexcept =
                [](const NS::Vector3& axis) noexcept -> bool {
                const float maxComponent = std::max({std::abs(axis.x), std::abs(axis.y), std::abs(axis.z)});
                return maxComponent >= 1.0f - k_AlignEpsilon;
            };
            return alignedAxis(obb.axisX) && alignedAxis(obb.axisY) && alignedAxis(obb.axisZ);
        }

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

    BoxCollision::BoxCollision() noexcept {}

    BoxCollision::BoxCollision(const NS::Vector3& halfExtents) noexcept
        : m_halfExtents(ClampNonNegative(halfExtents))
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

    NS::Matrix BoxCollision::LocalMatrix() const noexcept
    {
        return NS::Matrix::CreateFromQuaternion(m_localRotation) *
               NS::Matrix::CreateTranslation(m_centerOffset);
    }

    NS::Matrix BoxCollision::CombinedWorldMatrix() const noexcept
    {
        const Actor* owner = Owner();
        if (owner != nullptr)
        {
            return LocalMatrix() * owner->Root().WorldMatrix();
        }
        return LocalMatrix();
    }

    NS::AABB BoxCollision::WorldAABB() const noexcept
    {
        // 原点中心 + 半径の local box に、当たり箱の local offset / 回転 → owner の world 変換の順で重ねる
        // 回転時は内包する軸並行 AABB になる
        const NS::Matrix combined = CombinedWorldMatrix();
        const NS::AABB local(NS::Vector3{0.0f, 0.0f, 0.0f}, m_halfExtents);
        NS::AABB world;
        local.Transform(world, combined);
        return world;
    }

    NS::OBB BoxCollision::WorldOBB() const noexcept
    {
        const NS::AffineDecomposition decomposed = NS::DecomposeAffine(CombinedWorldMatrix());
        const NS::Vector3& scale = decomposed.scale;
        const NS::Quaternion& rotation = decomposed.rotation;
        const NS::Vector3& translation = decomposed.translation;
        const NS::Vector3 half{m_halfExtents.x * std::abs(scale.x),
                                     m_halfExtents.y * std::abs(scale.y),
                                     m_halfExtents.z * std::abs(scale.z)};

        return NS::MakeOBB(translation, rotation, half);
    }

    JPH::BodyID BoxCollision::SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        return physics.SyncBox(current, WorldOBB(), NS::Phys::ObjectLayers::Terrain);
    }

    NS_CLASS(BoxCollision)
} // namespace NS::Obj
