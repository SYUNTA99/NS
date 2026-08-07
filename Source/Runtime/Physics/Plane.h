#pragma once

#include "Runtime/Core/Math.h"

namespace NS::Physics
{
    /// @brief 法線と距離で表す無限平面
    /// @details dot(normal, p) = distance を満たす点集合
    struct Plane
    {
        NS::Core::Vector3 normal{0.0f, 1.0f, 0.0f}; // 面の法線、単位ベクトル
        float distance = 0.0f;                      // 原点から面までの符号付き距離
    };
} // namespace NS::Physics
