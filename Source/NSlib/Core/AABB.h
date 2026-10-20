#pragma once

#include "NSlib/Core/Math.h"

#include <DirectXCollision.h>

#include <cstddef>

namespace NS
{
    //! 軸に平行な境界ボックス
    using AABB = DirectX::BoundingBox;

    //! @brief 位置の並びを包む箱を返す
    //! @details stride は隣の位置までのバイト数で、頂点の型の中の位置を指して渡せる
    //! count が 0 の時は中心 0・大きさ 0 の箱 (DirectX の箱の既定は大きさ 1)
    [[nodiscard]] inline AABB AABBFromPositions(const Vector3* first, std::size_t count, std::size_t stride) noexcept
    {
        if (first == nullptr || count == 0u)
        {
            return AABB{Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.0f, 0.0f, 0.0f}};
        }
        AABB box{};
        DirectX::BoundingBox::CreateFromPoints(box, count, first, stride);
        return box;
    }
} // namespace NS
