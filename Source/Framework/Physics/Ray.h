#pragma once

/// @file Ray.h
/// @brief NS::Physics::Ray — NS::Core::Ray (SimpleMath::Ray) のレイヤエイリアス
/// SimpleMath ネイティブの `bool Intersects(BoundingBox, float&)` を直接利用する

#include "Framework/Core/Math.h"

namespace NS::Physics
{
    using Ray = NS::Core::Ray;
} // namespace NS::Physics
