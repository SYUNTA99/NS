#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Phys/Triangle.h"

#include <array>

namespace NS::Phys
{
        //! 描画と同じ規約の楔形スロープ。外向き CCW の 8 三角形
        //! angleDegrees は度、高さの上限は halfExtents.y × 2
        //! yawRadians はラジアン。0 で +Z 側が高い
        [[nodiscard]] std::array<Triangle, 8> BuildWedgeTriangles(const Vector3& center,
                                                                  const Vector3& halfExtents,
                                                                  float angleDegrees,
                                                                  float yawRadians) noexcept;
} // namespace NS::Phys
