#pragma once

/// @file plane.h
/// @brief ns::physics::Plane — Slope 表現用。 で CharacterController が利用。

#include "ns/core/math.h"

namespace ns::physics
{
    /// 無限平面: dot(normal, p) = distance を満たす点集合。
    struct Plane
    {
        ns::core::Vector3 normal{0.0f, 1.0f, 0.0f};
        float distance = 0.0f;
    };
} // namespace ns::physics
