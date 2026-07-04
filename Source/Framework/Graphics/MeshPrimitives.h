#pragma once

/// @file MeshPrimitives.h
/// @brief 基本プリミティブ cube / plane の頂点/index データ生成
///
/// 生成された `MeshGeometry` の vector を `MeshDesc` に渡して `StaticMesh` を構築する
/// MeshGeometry の生存中のみ MeshDesc::vertices/indices は有効。StaticMesh コンストラクタ内で
/// GPU upload されるため、コンストラクタ完了後は破棄して良い

#include "Framework/Graphics/StaticMesh.h"
#include "Framework/Math/Math.h"

#include <cstdint>
#include <vector>

namespace NS::Graphics
{
    /// CPU 側の頂点/index データ。生存期間が `MeshDesc` の pointer と同じ
    struct MeshGeometry
    {
        std::vector<StaticVertex> vertices;
        std::vector<std::uint32_t> indices;
    };

    /// 立方体プリミティブ。per-face normal で 24 vertex + 36 index、CW = front
    /// vector 確保で std::bad_alloc が伝搬する可能性があるため noexcept は付けない
    [[nodiscard]] MeshGeometry MakeCube(const NS::Math::Vector3& extents);

    /// XZ 平面で Y=0 上向き、4 vertex + 6 index、normal=+Y
    [[nodiscard]] MeshGeometry MakePlane(const NS::Math::Vector2& extents);

    /// +Z 上昇スロープ付き楔形 5 面体。angleDegrees は傾斜角、extents.y は最大高さ上限。16 vertex + 24 index
    [[nodiscard]] MeshGeometry MakeWedge(float angleDegrees, const NS::Math::Vector3& extents);
} // namespace NS::Graphics
