#pragma once

#include "NSlib/Core/Math.h"

namespace NS
{
    //! 球
    struct Sphere
    {
        Vector3 center{0.0f, 0.0f, 0.0f};
        float radius = 0.5f;
    };
} // namespace NS
