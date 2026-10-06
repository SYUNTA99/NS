#pragma once

#include "NSlib/Core/Math.h"

#include <cmath>

namespace NS::Game::Player
{
    //! @brief 水平の向き from を Y 軸まわりに radians だけ回す
    //! @details 正の角度は +X を -Z の側へ回す
    [[nodiscard]] inline NS::Vector3 RotateHorizontal(const NS::Vector3& from, float radians) noexcept
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return NS::Vector3{from.x * c + from.z * s, 0.0f, -from.x * s + from.z * c};
    }

    //! from を RotateHorizontal で回して to へ重ねる角度を返す。範囲は -π..π
    [[nodiscard]] inline float HorizontalAngleBetween(const NS::Vector3& from,
                                                      const NS::Vector3& to) noexcept
    {
        return std::atan2(from.z * to.x - from.x * to.z, from.x * to.x + from.z * to.z);
    }
} // namespace NS::Game::Player
