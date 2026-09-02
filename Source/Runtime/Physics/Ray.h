#pragma once

#include "Runtime/Core/Math.h"

namespace NS::Physics
{
    //! @brief NS::Core::Ray の別名。bool Intersects(BoundingBox, float&) をそのまま使う
    using Ray = NS::Core::Ray;
} // namespace NS::Physics
