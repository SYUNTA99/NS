#pragma once

/// @file capsule.h
/// @brief ns::physics::Capsule — Player 衝突形状。
/// Cylinder 部 + 上下 hemisphere。 で SweptCapsuleVsAABB の入力に使う。

#include "ns/core/math.h"

namespace ns::physics
{
    /// 軸対称 capsule。center を中心に axis 方向に halfHeight 半身、上下 hemisphere は radius。
    struct Capsule
    {
        ns::core::Vector3 center{0.0f, 0.0f, 0.0f};
        ns::core::Vector3 axis{0.0f, 1.0f, 0.0f};
        float halfHeight = 0.5f;
        float radius = 0.4f;
    };
} // namespace ns::physics
