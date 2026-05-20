#pragma once

/// @file swept_aabb.h
/// @brief Swept Capsule vs AABB の TOI 計算。 で実装。

#include "ns/core/math.h"
#include "ns/physics/capsule.h"

namespace ns::physics
{
    /// @brief Capsule が motion ベクトルだけ移動した場合の AABB との最初の接触を返す。
    /// @param capsule  入力 Capsule (start 位置)。
    /// @param motion   1 frame の変位ベクトル。
    /// @param box      ターゲット AABB。
    /// @param outToi   [0,1] の接触時刻、no hit なら 1.0。
    /// @param outNormal 接触法線 (capsule 表面外向き)、no hit なら zero。
    /// @retresult true = 接触あり / false = no hit。
    [[nodiscard]] bool SweptCapsuleVsAABB(const Capsule& capsule,
                                          const ns::core::Vector3& motion,
                                          const ns::core::AABB& box,
                                          float& outToi,
                                          ns::core::Vector3& outNormal) noexcept;
} // namespace ns::physics
