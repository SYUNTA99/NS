#pragma once

#include "Runtime/Core/Math.h"

namespace NS::Phys
{
    //! 頂点を CCW に並べる前提の 1 三角形。法線は (v1 - v0) × (v2 - v0) の向き
    struct Triangle
    {
        NS::Core::Vector3 v0{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 v1{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 v2{0.0f, 0.0f, 0.0f};
    };
} // namespace NS::Phys
