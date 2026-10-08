#pragma once

#include "NSlib/Core/Math.h"

namespace NS::Phys
{
    //! @brief カプセル形状
    //! @details 円柱の両端に半球がついた形状
    struct Capsule
    {
        NS::Vector3 center{0.0f, 0.0f, 0.0f}; // 中心座標
        NS::Vector3 axis{0.0f, 1.0f, 0.0f};   // カプセルが伸びる方向
        float halfHeight = 0.5f;              // 中心から端の半球の中心までの距離
        float radius = 0.4f;                  // 半径
    };

    //! @brief 上の向きに伸びるカプセルに、回転と拡大を掛ける
    //! @details 横の拡大が x と z で違っても半径は 1 つで、絶対値の大きい方に合わせる
    [[nodiscard]] inline Capsule MakeScaledCapsule(const NS::Vector3& center,
                                                   const NS::Quaternion& rotation,
                                                   const NS::Vector3& scale,
                                                   float radius,
                                                   float halfHeight) noexcept
    {
        return Capsule{.center = center,
                       .axis = NS::Vector3::Transform(NS::Vector3::UnitY, rotation),
                       .halfHeight = halfHeight * std::abs(scale.y),
                       .radius = radius * std::max(std::abs(scale.x), std::abs(scale.z))};
    }
} // namespace NS::Phys
