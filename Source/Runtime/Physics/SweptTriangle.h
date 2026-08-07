#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Physics/Capsule.h"

namespace NS::Physics
{
    /// CCW winding 前提の 1 三角形。normal は `(v1 - v0) × (v2 - v0)` で求める
    struct Triangle
    {
        NS::Core::Vector3 v0{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 v1{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 v2{0.0f, 0.0f, 0.0f};
    };

    /// @brief Capsule が motion ベクトルだけ移動した場合の Triangle との最初の接触を返す
    /// @details 45 / 30 / 22.5 / 15 度の 4 種固定 wedge のみを想定し、角度ごとの特別扱いをせず 1 本で済ませる
    /// 床判定は呼出側で contactNormal.y > 0.7 を確認する。cos 45 はおよそ 0.707 で、それを上回る緩い面を歩ける床とする
    /// @param capsule  移動開始位置の Capsule
    /// @param motion   1 フレーム分の変位ベクトル
    /// @param tri      CCW winding のターゲット三角形
    /// @param outToi   [0, 1] の接触時刻。当たらなければ 1.0
    /// @param outNormal CCW で計算した三角形の表面 normal。当たらなければ零ベクトル
    /// @retresult true = 接触あり / false = 接触なし
    [[nodiscard]] bool SweptCapsuleVsTriangle(const NS::Physics::Capsule& capsule,
                                              const NS::Core::Vector3& motion,
                                              const Triangle& tri,
                                              float& outToi,
                                              NS::Core::Vector3& outNormal) noexcept;
} // namespace NS::Physics
