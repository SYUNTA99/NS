#pragma once

/// @file ClimbableSurfaceComponent.h
/// @brief 金網 / フェンス用 trigger volume Component。
///
/// @details Owner GameObject の root world transform を基準に、 厚みのある板状 AABB を
/// trigger 領域として返す。 Player::Movement の state machine は ContainsPoint で
/// 掴まり可否を判定し、 grab 後は default CharacterController を bypass して
/// 板面 (face plane) 上で 2D 移動させる。
/// 種別を `ClimbableKind::Fence` に予約してあるが、 v2 で one-way fence 等を派生させる
/// 余地として `Kind()` accessor を残す。 Pole は別 Component (`PoleComponent`) に分離。

#include "Framework/Core/Math.h"
#include "Framework/Scene/Component.h"

namespace NS::Scene
{
    /// 金網 / ポール等の掴まり面の種別。 Pole は別 Component で扱うが、
    /// 将来的に 1 つの enum で表現するための予約値として残す。
    enum class ClimbableKind
    {
        Fence,
        Pole,
    };

    /// 板状 (XY 面の薄い AABB) trigger Component。 Owner の root position を中心に、
    /// `halfExtents` で AABB サイズを定義する。 `faceNormal` は board 表面の法線で、
    /// Player の 2D 入力を face plane に投影するときの基底計算に使う。
    class ClimbableSurfaceComponent : public Component
    {
    public:
        /// @param owner          所有 GameObject。 base ctor で auto-register。
        /// @param kind           種別。 通常は `ClimbableKind::Fence`。
        /// @param halfExtents    板状 AABB の半サイズ (薄い面方向は 0.05~0.1 を想定)。
        /// @param faceNormal     表面法線 (Player が掴まる側、 normalize 必須)。
        ClimbableSurfaceComponent(GameObject* owner,
                                  ClimbableKind kind,
                                  const NS::Core::Vector3& halfExtents,
                                  const NS::Core::Vector3& faceNormal) noexcept;

        [[nodiscard]] ClimbableKind Kind() const noexcept { return m_kind; }
        [[nodiscard]] NS::Core::Vector3 HalfExtents() const noexcept { return m_halfExtents; }
        [[nodiscard]] NS::Core::Vector3 FaceNormal() const noexcept { return m_faceNormal; }

        /// Owner の root world position を中心とした AABB。 Owner 不在で origin 中心。
        [[nodiscard]] NS::Core::AABB WorldAABB() const noexcept;

        /// `worldPos` が AABB の内側か。 Owner 不在でも default AABB を使い null-safe に判定。
        [[nodiscard]] bool ContainsPoint(const NS::Core::Vector3& worldPos) const noexcept;

    private:
        ClimbableKind m_kind = ClimbableKind::Fence;
        NS::Core::Vector3 m_halfExtents{0.5f, 0.5f, 0.1f};
        NS::Core::Vector3 m_faceNormal{0.0f, 0.0f, 1.0f};
    };
} // namespace NS::Scene
