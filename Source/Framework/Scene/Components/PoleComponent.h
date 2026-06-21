#pragma once

/// @file PoleComponent.h
/// @brief ポール (cylinder trigger) Component
///
/// @details Owner の root transform を基準に axis (cylinder の上下端) を返す
/// Player は axis に沿って Y 移動し、 horizontal stick で軸まわり回転、
/// jump で離脱する。 Player の state machine が AxisStart/AxisEnd の補間と
/// ContainsPoint の判定を回して default CharacterController を bypass する

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"

namespace NS::Scene
{
    /// 縦置きの cylinder を trigger とする Component。 半径と高さは data として保持し、
    /// world 空間の上下端を `AxisStart()` / `AxisEnd()` で公開する
    class PoleComponent : public Component
    {
    public:
        /// @param radius   ポール半径 (m)。 default 0.15
        /// @param height   ポール全長 (m)。 default 2.0。 owner 中心から上下に半分ずつ伸びる
        PoleComponent(float radius, float height) noexcept;

        [[nodiscard]] float Radius() const noexcept { return m_radius; }
        [[nodiscard]] float Height() const noexcept { return m_height; }

        /// world 空間でのポール下端 (Y 方向に -height/2 オフセット)
        [[nodiscard]] NS::Math::Vector3 AxisStart() const noexcept;
        /// world 空間でのポール上端 (Y 方向に +height/2 オフセット)
        [[nodiscard]] NS::Math::Vector3 AxisEnd() const noexcept;

        /// XZ 距離が radius 以下かつ Y が AxisStart..AxisEnd 内なら true
        [[nodiscard]] bool ContainsPoint(const NS::Math::Vector3& worldPos) const noexcept;

        // ポール寸法を Inspector へ公開する。 掴み判定が毎フレーム読むのでライブで効く
        NS_REFLECT_BEGIN(PoleComponent)
        NS_REFLECT_FIELD(m_radius, "Radius")
        NS_REFLECT_FIELD(m_height, "Height")
        NS_REFLECT_END()

    private:
        float m_radius = 0.15f;
        float m_height = 2.0f;
    };
} // namespace NS::Scene
