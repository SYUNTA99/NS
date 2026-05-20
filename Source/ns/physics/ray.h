#pragma once

/// @file ray.h
/// @brief ns::physics::Ray — ns::core::Ray (SimpleMath::Ray) のレイヤエイリアス。
/// SimpleMath ネイティブの `bool Intersects(BoundingBox, float&)` を直接利用する。

#include "ns/core/math.h"

namespace ns::physics
{
    using Ray = ns::core::Ray;
} // namespace ns::physics
