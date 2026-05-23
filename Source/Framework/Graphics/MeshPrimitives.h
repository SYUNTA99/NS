#pragma once

/// @file MeshPrimitives.h
/// @brief 基本プリミティブの頂点/index データ生成 ( cube / plane)。
///
/// 生成された `MeshGeometry` の vector を `MeshDesc` に渡して `Mesh` を構築する。
/// MeshGeometry の生存中のみ MeshDesc::vertices/indices は有効。Mesh ctor 内で
/// GPU upload されるため、ctor 完了後は破棄して良い。

#include "Framework/Core/Math.h"
#include "Framework/Graphics/Mesh.h"

#include <cstdint>
#include <vector>

namespace NS::Graphics
{
    /// CPU 側の頂点/index データ。生存期間が `MeshDesc` の pointer と同じ。
    struct MeshGeometry
    {
        std::vector<MeshVertex> vertices;
        std::vector<std::uint16_t> indices;
    };

    /// 立方体プリミティブ。per-face normal で 24 vertex + 36 index、CW = front。
    /// vector 確保で std::bad_alloc が伝搬する可能性があるため noexcept は付けない。
    [[nodiscard]] MeshGeometry MakeCube(const NS::Core::Vector3& extents);

    /// XZ 平面 (Y=0 上向き)、4 vertex + 6 index、normal=+Y。
    [[nodiscard]] MeshGeometry MakePlane(const NS::Core::Vector2& extents);
} // namespace NS::Graphics
