#pragma once

/// @file SweptAABB.h
/// @brief Swept Capsule vs AABB の TOI 計算

#include "Framework/Core/Math.h"
#include "Framework/Physics/Capsule.h"

namespace NS::Physics
{
    /// @brief Capsule が motion ベクトルだけ移動した場合の AABB との最初の接触を返す
    /// @param capsule  入力 Capsule (start 位置)
    /// @param motion   1 frame の変位ベクトル
    /// @param box      ターゲット AABB
    /// @param outToi   [0,1] の接触時刻、no hit なら 1.0
    /// @param outNormal 接触法線 (capsule 表面外向き)、no hit なら zero
    /// @retresult true = 接触あり / false = no hit
    [[nodiscard]] bool SweptCapsuleVsAABB(const Capsule& capsule,
                                          const NS::Math::Vector3& motion,
                                          const NS::Math::AABB& box,
                                          float& outToi,
                                          NS::Math::Vector3& outNormal) noexcept;
} // namespace NS::Physics
