#pragma once

#include "Runtime/Core/Math.h"

namespace NS::Phys
{
    //! @brief カプセル形状
    //! @details 円柱の両端に半球がついた形状
    struct Capsule
    {
        NS::Core::Vector3 center{0.0f, 0.0f, 0.0f}; // 中心座標
        NS::Core::Vector3 axis{0.0f, 1.0f, 0.0f};   // カプセルが伸びる方向
        float halfHeight = 0.5f;                    // 中心から端の半球の中心までの距離
        float radius = 0.4f;                        // 半径
    };
} // namespace NS::Phys
