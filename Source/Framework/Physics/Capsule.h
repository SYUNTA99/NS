#pragma once

/// @file Capsule.h
/// @brief NS::Physics::Capsule — Player 衝突形状
/// Cylinder 部 + 上下 hemisphere。SweptCapsuleVsAABB の入力に使う

#include "Framework/Math/Math.h"

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

    /// 縦 capsule と AABB の重なり判定。芯線分と box の最短距離が radius 以下なら true
    /// @pre capsule.axis は Y 固定前提
    [[nodiscard]] bool IntersectsCapsuleAabb(const Capsule& capsule, const NS::Math::AABB& box) noexcept;
} // namespace NS::Physics
