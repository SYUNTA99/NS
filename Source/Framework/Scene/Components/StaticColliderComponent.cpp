#include "Framework/Scene/Components/StaticColliderComponent.h"

#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

#include <cmath>

namespace NS::Scene
{
    namespace
    {
        [[nodiscard]] NS::Math::Vector3 ClampNonNegative(const NS::Math::Vector3& v) noexcept
        {
            return NS::Math::Vector3{
                v.x < 0.0f ? 0.0f : v.x,
                v.y < 0.0f ? 0.0f : v.y,
                v.z < 0.0f ? 0.0f : v.z,
            };
        }
    } // namespace

    StaticColliderComponent::StaticColliderComponent() noexcept {}

    StaticColliderComponent::StaticColliderComponent(const NS::Math::Vector3& halfExtents) noexcept
        : m_halfExtents(ClampNonNegative(halfExtents))
    {}

    void StaticColliderComponent::SetHalfExtents(const NS::Math::Vector3& halfExtents) noexcept
    {
        m_halfExtents = ClampNonNegative(halfExtents);
    }

    NS::Math::Vector3 StaticColliderComponent::HalfExtents() const noexcept
    {
        return m_halfExtents;
    }

    void StaticColliderComponent::SetCenterOffset(const NS::Math::Vector3& offset) noexcept
    {
        m_centerOffset = offset;
    }

    NS::Math::Vector3 StaticColliderComponent::CenterOffset() const noexcept
    {
        return m_centerOffset;
    }

    void StaticColliderComponent::SetLocalRotation(const NS::Math::Quaternion& rotation) noexcept
    {
        m_localRotation = rotation;
    }

    NS::Math::Quaternion StaticColliderComponent::LocalRotation() const noexcept
    {
        return m_localRotation;
    }

    void StaticColliderComponent::SetRotationEulerDegrees(const NS::Math::Vector3& eulerDegrees) noexcept
    {
        m_localRotation =
            NS::Math::Quaternion::CreateFromYawPitchRoll(NS::Math::Vector3{NS::Math::DegreesToRadians(eulerDegrees.x),
                                                                           NS::Math::DegreesToRadians(eulerDegrees.y),
                                                                           NS::Math::DegreesToRadians(eulerDegrees.z)});
    }

    NS::Math::Vector3 StaticColliderComponent::RotationEulerDegrees() const noexcept
    {
        const NS::Math::Vector3 euler = m_localRotation.ToEuler();
        return NS::Math::Vector3{NS::Math::RadiansToDegrees(euler.x),
                                 NS::Math::RadiansToDegrees(euler.y),
                                 NS::Math::RadiansToDegrees(euler.z)};
    }

    NS::Math::Matrix StaticColliderComponent::LocalMatrix() const noexcept
    {
        return NS::Math::Matrix::CreateFromQuaternion(m_localRotation) *
               NS::Math::Matrix::CreateTranslation(m_centerOffset);
    }

    NS::Math::AABB StaticColliderComponent::WorldAABB() const noexcept
    {
        const GameObject* owner = Owner();
        // local box (原点中心 + 半径) に、 当たり箱の local offset / 回転 → owner の world 変換の順で重ねる
        // grid (offset 0・回転単位・scale 1・整数位置) では結果が従来と一致する。 回転時は内包する軸並行 AABB になる
        const NS::Math::Matrix combined =
            (owner != nullptr) ? LocalMatrix() * owner->Root().WorldMatrix() : LocalMatrix();
        const NS::Math::AABB local(NS::Math::Vector3{0.0f, 0.0f, 0.0f}, m_halfExtents);
        NS::Math::AABB world;
        local.Transform(world, combined);
        return world;
    }

    NS::Physics::OBB StaticColliderComponent::WorldOBB() const noexcept
    {
        const GameObject* owner = Owner();
        // Decompose は非 const のためローカルは mutable で持つ
        NS::Math::Matrix combined = (owner != nullptr) ? LocalMatrix() * owner->Root().WorldMatrix() : LocalMatrix();

        NS::Math::Vector3 scale{1.0f, 1.0f, 1.0f};
        NS::Math::Quaternion rotation = NS::Math::Quaternion::Identity;
        NS::Math::Vector3 translation{0.0f, 0.0f, 0.0f};
        combined.Decompose(scale, rotation, translation);

        const NS::Math::Vector3 half{m_halfExtents.x * std::abs(scale.x),
                                     m_halfExtents.y * std::abs(scale.y),
                                     m_halfExtents.z * std::abs(scale.z)};
        return NS::Physics::MakeObb(translation, rotation, half);
    }
} // namespace NS::Scene
