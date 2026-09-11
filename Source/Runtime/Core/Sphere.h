#pragma once

#include "Runtime/Core/Math.h"

namespace NS::Core
{
    //! 球
    struct Sphere
    {
        Vector3 center{0.0f, 0.0f, 0.0f};
        float radius = 0.5f;
    };
} // namespace NS::Core
