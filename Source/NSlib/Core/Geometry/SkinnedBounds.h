#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Geometry/Vertex.h"

#include <cstddef>
#include <vector>

namespace NS
{
    //! バインド空間の影響頂点を包む球。姿勢に追従する境界の計算に使う
    struct BoneSphere
    {
        Vector3 center{0.0f, 0.0f, 0.0f}; // バインド空間での影響頂点の中心
        float radius = 0.0f;              // 中心から最遠の影響頂点までの距離、負なら影響頂点なし
    };

    //! 重みが正の頂点から、ボーンごとの影響球を求める。影響のないボーンは負の半径
    [[nodiscard]] std::vector<BoneSphere> ComputeBoneSpheres(const SkinnedVertex* vertices,
                                                             std::size_t vertexCount,
                                                             std::size_t boneCount);

    //! 姿勢に追従する球で境界を保守的に包む。影響球がなければ fallback
    [[nodiscard]] AABB MergeSkinnedBounds(const std::vector<BoneSphere>& spheres,
                                          const Matrix* palette,
                                          std::size_t paletteCount,
                                          const AABB& fallback);
} // namespace NS
