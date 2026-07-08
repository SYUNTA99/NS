#pragma once

/// @file CapsuleColliderComponent.h
/// @brief カプセル collider Component。 owner の world 変換から中心・軸・半径・半高を備えた WorldCapsule を返す

#include "Framework/Math/Math.h"
#include "Framework/Physics/Capsule.h"
#include "Framework/Scene/Component.h"

namespace NS::Scene
{
    /// カプセル collider を SceneBase に登録する Component
    /// 既定は縦向き Y 軸の capsule。 local 回転で寝かせられる。 半径は X/Z scale の最大、 半高は Y scale で拡縮する
    /// 回転と非一様 scale を併用すると近似になる
    class CapsuleColliderComponent : public Component
    {
    public:
        /// 既定 半径 0.4 / 半高 0.5 の縦 capsule で構築する
        CapsuleColliderComponent() noexcept;
        /// 半径 / 半高を指定して構築する。 負は最小値にクランプ
        CapsuleColliderComponent(float radius, float halfHeight) noexcept;

        /// 負は 0 にクランプ
        void SetRadius(float radius) noexcept;
        [[nodiscard]] float Radius() const noexcept;

        /// 芯の半分の長さ、 半球を除く円柱部の半長。 負は 0 にクランプ
        void SetHalfHeight(float halfHeight) noexcept;
        [[nodiscard]] float HalfHeight() const noexcept;

        /// owner local 空間での中心オフセット
        void SetCenterOffset(const NS::Math::Vector3& offset) noexcept;
        [[nodiscard]] NS::Math::Vector3 CenterOffset() const noexcept;

        /// local 回転を quaternion で直接設定 / 取得する。 保存値の復元に使う
        void SetLocalRotation(const NS::Math::Quaternion& rotation) noexcept;
        [[nodiscard]] NS::Math::Quaternion LocalRotation() const noexcept;

        /// local 回転を pitch/yaw/roll の Euler 角を度数法で読み書きする。 寝かせ用に内部は quaternion 保持
        void SetRotationEulerDegrees(const NS::Math::Vector3& eulerDegrees) noexcept;
        [[nodiscard]] NS::Math::Vector3 RotationEulerDegrees() const noexcept;

        /// owner の world 変換に local offset / 回転を重ねた capsule を返す
        /// axis は local 回転で回した Y 軸、 半径は X/Z scale 最大、 半高は Y scale で拡縮する
        /// Owner 未登録時は local offset / 回転だけを反映する。 例外は投げない
        [[nodiscard]] NS::Physics::Capsule WorldCapsule() const noexcept;

        /// owner の world 変換を反映した世界軸並行 AABB を返す。 Owner 未登録時は local だけを反映する
        [[nodiscard]] NS::Math::AABB WorldAABB() const noexcept;

        NS_REFLECT_BEGIN(CapsuleColliderComponent, Component)
        NS_REFLECT_ACCESSOR(float, "Radius", Radius(), SetRadius)
        NS_REFLECT_ACCESSOR(float, "Half Height", HalfHeight(), SetHalfHeight)
        NS_REFLECT_ACCESSOR(NS::Math::Vector3, "Center Offset", CenterOffset(), SetCenterOffset)
        NS_REFLECT_ACCESSOR(NS::Math::Vector3, "Rotation (deg)", RotationEulerDegrees(), SetRotationEulerDegrees)
        NS_REFLECT_END()

    private:
        float m_radius = 0.4f;
        float m_halfHeight = 0.5f;
        NS::Math::Vector3 m_centerOffset{0.0f, 0.0f, 0.0f};
        NS::Math::Quaternion m_localRotation = NS::Math::Quaternion::Identity;
    };
} // namespace NS::Scene
