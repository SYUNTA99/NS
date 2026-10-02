#pragma once

#include "Runtime/Core/Math.h"

#include <cmath>

namespace NS::Game::Player
{
    //! @brief 水平の向き from を Y 軸まわりに radians だけ回す
    //! @details 正の角度は +X を -Z の側へ回す
    [[nodiscard]] inline NS::Core::Vector3 RotateHorizontal(const NS::Core::Vector3& from, float radians) noexcept
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return NS::Core::Vector3{from.x * c + from.z * s, 0.0f, -from.x * s + from.z * c};
    }

    //! from を RotateHorizontal で回して to へ重ねる角度を返す。範囲は -π..π
    [[nodiscard]] inline float HorizontalAngleBetween(const NS::Core::Vector3& from,
                                                      const NS::Core::Vector3& to) noexcept
    {
        return std::atan2(from.z * to.x - from.x * to.z, from.x * to.x + from.z * to.z);
    }

    //! @brief from を Y 軸まわりに最大 maxRadians だけ to へ寄せた向きを返す
    //! @param[in] from 正規化した水平の向き
    //! @param[in] to 正規化した水平の向き
    [[nodiscard]] inline NS::Core::Vector3 TurnHorizontalToward(const NS::Core::Vector3& from,
                                                                const NS::Core::Vector3& to,
                                                                float maxRadians) noexcept
    {
        const float angle = HorizontalAngleBetween(from, to);
        if (std::abs(angle) <= maxRadians)
        {
            return to;
        }
        float step = maxRadians;
        if (angle < 0.0f)
        {
            step = -maxRadians;
        }
        return RotateHorizontal(from, step);
    }
} // namespace NS::Game::Player
