#pragma once

/// @file Plane.h
/// @brief NS::Physics::Plane — Slope 表現用。 で CharacterController が利用。

#include "Framework/Core/Math.h"

namespace NS::Physics
{
    /// 無限平面: dot(normal, p) = distance を満たす点集合。
    struct Plane
    {
        NS::Core::Vector3 normal{0.0f, 1.0f, 0.0f};
        float distance = 0.0f;
    };
} // namespace NS::Physics
