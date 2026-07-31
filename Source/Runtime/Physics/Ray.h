#pragma once

#include "Runtime/Math/Math.h"

namespace NS::Physics
{
    /// @brief NS::Math::Ray の別名。`bool Intersects(BoundingBox, float&)` をそのまま使う
    using Ray = NS::Math::Ray;
} // namespace NS::Physics
