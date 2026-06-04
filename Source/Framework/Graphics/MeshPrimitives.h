#pragma once

/// @file MeshPrimitives.h
/// @brief 基本プリミティブの頂点/index データ生成 (cube / plane)
///
/// 生成された `MeshGeometry` の vector を `MeshDesc` に渡して `Mesh` を構築する
/// MeshGeometry の生存中のみ MeshDesc::vertices/indices は有効。Mesh コンストラクタ内で
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
        std::vector<MeshVertex> vertices;
        std::vector<std::uint16_t> indices;
    };

    /// 立方体プリミティブ。per-face normal で 24 vertex + 36 index、CW = front
    /// vector 確保で std::bad_alloc が伝搬する可能性があるため noexcept は付けない
    [[nodiscard]] MeshGeometry MakeCube(const NS::Math::Vector3& extents);

    /// XZ 平面 (Y=0 上向き)、4 vertex + 6 index、normal=+Y
    [[nodiscard]] MeshGeometry MakePlane(const NS::Math::Vector2& extents);

    /// 楔形 (wedge) スロープ mesh。 +Z 方向に上昇する slope を持つ 5 面体
    /// `angleDegrees` は slope の傾斜角 (想定値: 45 / 30 / 22.5 / 15 度)
    /// `extents.x / extents.z` は底面の半サイズ、 `extents.y` は最大高さ上限
    /// (実際の高さは `min(extents.y, tan(angle) * extents.z * 2.0)`)
    /// 5 face = slope quad + bottom quad + back quad + 左右 triangle 2 個
    /// per-face normal で 16 vertex + 24 index 構成
    [[nodiscard]] MeshGeometry MakeWedge(float angleDegrees, const NS::Math::Vector3& extents);

    /// 円柱メッシュ (ポール用)。 `radius` 半径、 `height` 縦の全長、
    /// `segments` で側面の分割数 (default 12、 推奨 8~16)。 top / bottom cap + side strip の構成で
    /// per-face normal を発行する (なめらかな円柱ではなく per-segment flat shading)
    /// 中心は原点、 axis は Y 方向で上下に `height/2` ずつ伸びる
    [[nodiscard]] MeshGeometry MakeCylinder(float radius, float height, int segments = 12);
} // namespace NS::Graphics
