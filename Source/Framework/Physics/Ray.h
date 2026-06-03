#pragma once

/// @file Ray.h
/// @brief NS::Physics::Ray — NS::Math::Ray (SimpleMath::Ray) のレイヤエイリアス
/// SimpleMath ネイティブの `bool Intersects(BoundingBox, float&)` を直接利用する

#include "Framework/Math/Math.h"

namespace NS::Physics
{
    using Ray = NS::Math::Ray;
} // namespace NS::Physics
