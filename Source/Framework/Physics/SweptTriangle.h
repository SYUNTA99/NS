#pragma once

/// @file SweptTriangle.h
/// @brief NS::Physics::SweptTriangle — Capsule vs 三角形の TOI 計算 (wedge slope 用)。
///
/// @details 4 種固定 wedge (45 / 30 / 22.5 / 15 度) のみを想定し、 1 本の Routine で済ませる。
/// 角度別 special casing は意図的に避ける。 三角形は CCW winding 前提
/// (`normal = normalize(cross(v1 - v0, v2 - v0))`)。 floor 判定は呼出側で
/// `contactNormal.y > 0.7` を確認 (cos 45 ≈ 0.707、 cos 45 以下を walkable 床と定義)。

#include "Framework/Core/Math.h"
#include "Framework/Physics/Capsule.h"

namespace NS::Physics
{
    /// 1 三角形 (CCW winding 前提)。 normal は `(v1 - v0) × (v2 - v0)` で取得する。
    struct Triangle
    {
        NS::Core::Vector3 v0{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 v1{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 v2{0.0f, 0.0f, 0.0f};
    };

    /// @brief Capsule が motion ベクトルだけ移動した場合の Triangle との最初の接触を返す。
    /// @param capsule  入力 Capsule (start 位置)。
    /// @param motion   1 frame の変位ベクトル。
    /// @param tri      ターゲット三角形 (CCW winding)。
    /// @param outToi   [0, 1) の接触時刻、 no hit なら 1.0。
    /// @param outNormal 三角形の表面 normal (CCW で計算)、 no hit なら zero。
    /// @retresult true = 接触あり / false = no hit。
    ///
    /// @details Capsule を上下 2 個の sphere 端点として扱う簡易実装
    /// (Mario 系 platformer の固定 wedge 4 種のみが要件のため十分な近似)。
    /// 各端点に対し triangle 平面までの swept TOI を計算し、 接触点が三角形内
    /// (barycentric coordinate で判定) なら hit と返す。 2 端点のうち TOI が
    /// 小さい方を採用する。 motion が triangle 表面から離れる方向なら no hit。
    [[nodiscard]] bool SweptCapsuleVsTriangle(const Capsule& capsule,
                                              const NS::Core::Vector3& motion,
                                              const Triangle& tri,
                                              float& outToi,
                                              NS::Core::Vector3& outNormal) noexcept;
} // namespace NS::Physics
