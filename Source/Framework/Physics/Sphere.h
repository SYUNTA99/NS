#pragma once

/// @file Sphere.h
/// @brief NS::Physics::Sphere — center と radius による球コライダー形状

#include "Framework/Math/Math.h"

namespace NS::Physics
{
    /// 軸非依存の球。 center を中心に radius
    struct Sphere
    {
        NS::Math::Vector3 center{0.0f, 0.0f, 0.0f};
        float radius = 0.5f;
    };
} // namespace NS::Physics
