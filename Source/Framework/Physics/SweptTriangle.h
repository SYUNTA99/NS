#pragma once

/// @file SweptTriangle.h
/// @brief NS::Physics::SweptTriangle — wedge slope 用の Capsule vs 三角形の TOI 計算
///
/// @details 45 / 30 / 22.5 / 15 度の 4 種固定 wedge のみを想定し、 1 本の Routine で済ませる
/// 角度別 special casing は意図的に避ける。 三角形は CCW winding 前提で
/// normal は `normalize(cross(v1 - v0, v2 - v0))` で求める。 floor 判定は呼出側で
/// `contactNormal.y > 0.7` を確認する。 cos 45 はおよそ 0.707 で cos 45 以下を walkable 床と定義する

#include "Framework/Math/Math.h"
#include "Framework/Physics/Capsule.h"

namespace NS::Physics
{
    /// CCW winding 前提の 1 三角形。 normal は `(v1 - v0) × (v2 - v0)` で取得する
    struct Triangle
    {
        NS::Math::Vector3 v0{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 v1{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 v2{0.0f, 0.0f, 0.0f};
    };

    /// @brief Capsule が motion ベクトルだけ移動した場合の Triangle との最初の接触を返す
    /// @param capsule  start 位置の入力 Capsule
    /// @param motion   1 frame の変位ベクトル
    /// @param tri      CCW winding のターゲット三角形
    /// @param outToi   [0, 1) の接触時刻、 no hit なら 1.0
    /// @param outNormal CCW で計算した三角形の表面 normal、 no hit なら zero
    /// @retresult true = 接触あり / false = no hit
    [[nodiscard]] bool SweptCapsuleVsTriangle(const Capsule& capsule,
                                              const NS::Math::Vector3& motion,
                                              const Triangle& tri,
                                              float& outToi,
                                              NS::Math::Vector3& outNormal) noexcept;
} // namespace NS::Physics
