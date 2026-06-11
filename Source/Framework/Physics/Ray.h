#pragma once

/// @file Ray.h
/// @brief NS::Physics::Ray — NS::Math::Ray のレイヤエイリアス
/// `bool Intersects(BoundingBox, float&)` を直接利用する

#include "Framework/Math/Math.h"

namespace NS::Physics
{
    using Ray = NS::Math::Ray;
} // namespace NS::Physics
