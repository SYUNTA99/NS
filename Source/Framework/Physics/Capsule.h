#pragma once

/// @file Capsule.h
/// @brief NS::Physics::Capsule — Player 衝突形状
/// Cylinder 部 + 上下 hemisphere。SweptCapsuleVsAABB の入力に使う

#include "Framework/Core/Math.h"

namespace NS::Physics
{
    /// 軸対称 capsule。center を中心に axis 方向に halfHeight 半身、上下 hemisphere は radius
    struct Capsule
    {
        NS::Math::Vector3 center{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 axis{0.0f, 1.0f, 0.0f};
        float halfHeight = 0.5f;
        float radius = 0.4f;
    };

    /// 縦 (Y 軸) capsule と AABB の重なり判定
    /// capsule の芯線分 [center ± halfHeight·Y] と box の最近距離が radius 以下なら true
    /// @pre capsule.axis は Y 固定前提 (Player capsule は常に縦)
    /// @details solid 衝突は中心を box 表面から radius ぶん外に保つため、 中心点が box 内かどうかでは
    /// 接触を検出できない。 芯線分から box までの最近距離で判定することで「触れている」 状態を正しく拾う
    [[nodiscard]] bool IntersectsCapsuleAabb(const Capsule& capsule, const NS::Math::AABB& box) noexcept;
} // namespace NS::Physics
