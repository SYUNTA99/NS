#pragma once

/// @file Math.h
/// @brief 互換シム。数学型は NS::Math へ移設済。新規コードは NS::Math を使う

#include "Framework/Math/Math.h"

namespace NS::Core
{
    using Vector2 = NS::Math::Vector2;
    using Vector3 = NS::Math::Vector3;
    using Vector4 = NS::Math::Vector4;
    using Matrix = NS::Math::Matrix;
    using Matrix4x4 = NS::Math::Matrix4x4;
    using Quaternion = NS::Math::Quaternion;
    using Plane = NS::Math::Plane;
    using Ray = NS::Math::Ray;
    using Viewport = NS::Math::Viewport;
    using Color = NS::Math::Color;
    using AABB = NS::Math::AABB;
    using Radians = NS::Math::Radians;
    using Degrees = NS::Math::Degrees;
    using Size2D = NS::Math::Size2D;

    using NS::Math::kPi;
    using NS::Math::Deg2Rad;
    using NS::Math::Rad2Deg;
    using NS::Math::ToRadians;
    using NS::Math::ToDegrees;
    using NS::Math::AspectRatio;
    using NS::Math::Clamp;
    using NS::Math::Lerp;
} // namespace NS::Core
