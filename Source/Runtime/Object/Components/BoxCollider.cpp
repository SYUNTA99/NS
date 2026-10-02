#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/OBB.h"

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>
#include <cmath>

namespace NS::Obj
{
    namespace
    {
        [[nodiscard]] NS::Core::Vector3 ClampNonNegative(const NS::Core::Vector3& v) noexcept
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
            return NS::Core::Vector3{x, y, z};
        }
    } // namespace

    BoxCollider::BoxCollider() noexcept {}

    void BoxCollider::SetHalfExtents(const NS::Core::Vector3& halfExtents) noexcept
    {
        m_halfExtents = ClampNonNegative(halfExtents);
    }

    NS::Core::Vector3 BoxCollider::HalfExtents() const noexcept
    {
        return m_halfExtents;
    }

    void BoxCollider::SetCenterOffset(const NS::Core::Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    NS::Core::Vector3 BoxCollider::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    void BoxCollider::SetRotationEulerDegrees(const NS::Core::Vector3& eulerDegrees) noexcept
    {
        m_localRotation = NS::Core::EulerDegreesToQuaternion(eulerDegrees);
    }

    NS::Core::Vector3 BoxCollider::RotationEulerDegrees() const noexcept
    {
        return NS::Core::QuaternionToEulerDegrees(m_localRotation);
    }

    NS::Core::Matrix BoxCollider::LocalMatrix() const noexcept
    {
        return NS::Core::Matrix::CreateFromQuaternion(m_localRotation) *
               NS::Core::Matrix::CreateTranslation(m_centerOffset);
    }

    NS::Core::Matrix BoxCollider::CombinedWorldMatrix() const noexcept
    {
        const Actor* owner = Owner();
        if (owner != nullptr)
        {
            return LocalMatrix() * owner->Root().WorldMatrix();
        }
        return LocalMatrix();
    }

    NS::Core::AABB BoxCollider::WorldAABB() const noexcept
    {
        // 原点中心 + 半径の local box に、当たり箱の local offset / 回転 → owner の world 変換の順で重ねる
        // 回転時は内包する軸並行 AABB になる
        const NS::Core::Matrix combined = CombinedWorldMatrix();
        const NS::Core::AABB local(NS::Core::Vector3{0.0f, 0.0f, 0.0f}, m_halfExtents);
        NS::Core::AABB world;
        local.Transform(world, combined);
        return world;
    }

    NS::Core::OBB BoxCollider::WorldOBB() const noexcept
    {
        const NS::Core::AffineDecomposition decomposed = NS::Core::DecomposeAffine(CombinedWorldMatrix());
        const NS::Core::Vector3& scale = decomposed.scale;
        const NS::Core::Quaternion& rotation = decomposed.rotation;
        const NS::Core::Vector3& translation = decomposed.translation;
        const NS::Core::Vector3 half{m_halfExtents.x * std::abs(scale.x),
                                     m_halfExtents.y * std::abs(scale.y),
                                     m_halfExtents.z * std::abs(scale.z)};

        return NS::Core::MakeOBB(translation, rotation, half);
    }

    NS::Phys::ShapePart BoxCollider::RigidBodyPart() const
    {
        return NS::Phys::MakeBoxPart(WorldOBB());
    }

    JPH::BodyID BoxCollider::SyncBody(NS::Phys::PhysicsScene& physics, JPH::BodyID current)
    {
        return physics.SyncBox(current, WorldOBB(), NS::Phys::ObjectLayers::Terrain);
    }

    NS_CLASS(BoxCollider)
} // namespace NS::Obj
