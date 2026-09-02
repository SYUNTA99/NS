#include "Runtime/Object/Components/BoxColliderComponent.h"

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>

namespace NS::Object
{
    namespace
    {
        // OBB の 3 軸が座標軸に十分沿っていれば軸並行とみなす。 90° 刻みの回転はここに落ちる
        // 各軸は単位ベクトルなので最大成分が 1 に届けば残り 2 成分はほぼ 0 になる
        // しきい 1e-4 は 90° を quaternion 経由で組んだ時の float 誤差を確実に飲み込み、 1° 以上の傾きは OBB へ回す
        [[nodiscard]] bool IsAxisAligned(const NS::Core::OBB& obb) noexcept
        {
            constexpr float k_AlignEpsilon = 1e-4f;
            const auto alignedAxis = [](const NS::Core::Vector3& axis) noexcept {
                const float maxComponent = std::max({std::abs(axis.x), std::abs(axis.y), std::abs(axis.z)});
                return maxComponent >= 1.0f - k_AlignEpsilon;
            };
            return alignedAxis(obb.axisX) && alignedAxis(obb.axisY) && alignedAxis(obb.axisZ);
        }

        [[nodiscard]] NS::Core::Vector3 ClampNonNegative(const NS::Core::Vector3& v) noexcept
        {
            float x = v.x;
            if (x < 0.0f)
                x = 0.0f;
            float y = v.y;
            if (y < 0.0f)
                y = 0.0f;
            float z = v.z;
            if (z < 0.0f)
                z = 0.0f;
            return NS::Core::Vector3{x, y, z};
        }
    } // namespace

    BoxColliderComponent::BoxColliderComponent() noexcept {}

    BoxColliderComponent::BoxColliderComponent(const NS::Core::Vector3& halfExtents) noexcept
        : m_halfExtents(ClampNonNegative(halfExtents))
    {}

    void BoxColliderComponent::SetHalfExtents(const NS::Core::Vector3& halfExtents) noexcept
    {
        m_halfExtents = ClampNonNegative(halfExtents);
    }

    NS::Core::Vector3 BoxColliderComponent::HalfExtents() const noexcept
    {
        return m_halfExtents;
    }

    void BoxColliderComponent::SetCenterOffset(const NS::Core::Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    NS::Core::Vector3 BoxColliderComponent::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    void BoxColliderComponent::SetLocalRotation(const NS::Core::Quaternion& rotation) noexcept
    {
        m_localRotation = rotation;
    }

    NS::Core::Quaternion BoxColliderComponent::LocalRotation() const noexcept
    {
        return m_localRotation;
    }

    void BoxColliderComponent::SetRotationEulerDegrees(const NS::Core::Vector3& eulerDegrees) noexcept
    {
        m_localRotation = NS::Core::EulerDegreesToQuaternion(eulerDegrees);
    }

    NS::Core::Vector3 BoxColliderComponent::RotationEulerDegrees() const noexcept
    {
        return NS::Core::QuaternionToEulerDegrees(m_localRotation);
    }

    void BoxColliderComponent::SetTrigger(bool isTrigger) noexcept
    {
        m_isTrigger = isTrigger;
    }

    bool BoxColliderComponent::IsTrigger() const noexcept
    {
        return m_isTrigger;
    }

    NS::Core::Matrix BoxColliderComponent::LocalMatrix() const noexcept
    {
        return NS::Core::Matrix::CreateFromQuaternion(m_localRotation) *
               NS::Core::Matrix::CreateTranslation(m_centerOffset);
    }

    NS::Core::Matrix BoxColliderComponent::CombinedWorldMatrix() const noexcept
    {
        const GameObject* owner = Owner();
        if (owner != nullptr)
            return LocalMatrix() * owner->Root().WorldMatrix();
        return LocalMatrix();
    }

    NS::Core::AABB BoxColliderComponent::WorldAABB() const noexcept
    {
        // 原点中心 + 半径の local box に、 当たり箱の local offset / 回転 → owner の world 変換の順で重ねる
        // offset 0・回転単位・scale 1・整数位置の grid では結果が従来と一致する。 回転時は内包する軸並行 AABB になる
        const NS::Core::Matrix combined = CombinedWorldMatrix();
        const NS::Core::AABB local(NS::Core::Vector3{0.0f, 0.0f, 0.0f}, m_halfExtents);
        NS::Core::AABB world;
        local.Transform(world, combined);
        return world;
    }

    NS::Core::OBB BoxColliderComponent::WorldOBB() const noexcept
    {
        const auto [scale, rotation, translation] = NS::Core::DecomposeAffine(CombinedWorldMatrix());
        const NS::Core::Vector3 half{m_halfExtents.x * std::abs(scale.x),
                                     m_halfExtents.y * std::abs(scale.y),
                                     m_halfExtents.z * std::abs(scale.z)};
        return NS::Physics::MakeOBB(translation, rotation, half);
    }

    void BoxColliderComponent::AddToPhysics(NS::Physics::PhysicsWorld& physics) const
    {
        // トリガの箱は通り抜ける体積。 固形に入れず、 重なりは owner の component が自分で調べる
        if (m_isTrigger)
            return;

        const NS::Core::OBB obb = WorldOBB();
        if (IsAxisAligned(obb))
            physics.AddAABB(WorldAABB());
        else
            physics.AddOBB(obb);
    }

    NS_CLASS(BoxColliderComponent)
} // namespace NS::Object
