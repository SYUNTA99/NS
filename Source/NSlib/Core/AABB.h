#pragma once

#include "NSlib/Core/Math.h"

#include <DirectXCollision.h>

#include <cstddef>

namespace NS
{
    using AABB = DirectX::BoundingBox;

    //! stride は隣接要素までのバイト数。count が 0 または first が nullptr の場合は中心 0・半径 0 の箱を返す
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
