#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/StaticMesh.h"

namespace NS::Gfx
{
    //! GPU メッシュ構築用の CPU データ。構築後は破棄してよい
    struct MeshGeometry
    {
        std::vector<StaticVertex> vertices; // 頂点データ
        std::vector<std::uint32_t> indices; // index データ
        NS::AABB bounds{};            // 頂点構築時に広げた軸並行境界
        bool hasBounds = false;             // 有効なら StaticMesh がこれを使い、無ければ頂点から算出
    };

    //! extents は各軸の半幅。面ごとに独立した法線を持つ
    [[nodiscard]] MeshGeometry MakeCube(const NS::Vector3& extents);

    //! 原点中心の XZ 平面。extents は X・Z 方向の半幅
    [[nodiscard]] MeshGeometry MakePlane(const NS::Vector2& extents);

    //! +Z 側が高い斜面。angleDegrees は度、extents は各軸の半幅
    [[nodiscard]] MeshGeometry MakeSlope(float angleDegrees, const NS::Vector3& extents);

    //! @brief 原点中心の球のジオメトリを生成する
    //! @param[in] rings 上下方向の分割数。2 未満は 2 に切り上げる
    //! @param[in] segments 周方向の分割数。3 未満は 3 に切り上げる
    [[nodiscard]] MeshGeometry MakeSphere(float radius, std::uint32_t rings = 16, std::uint32_t segments = 32);

    //! Y 軸方向のカプセル。halfHeight は半球を含まない円柱部の半長で、0 なら球
    //! 負の寸法は 0、分割数は rings が 1、segments が 3 を下限とする
    [[nodiscard]] MeshGeometry MakeCapsule(float radius,
                                           float halfHeight,
                                           std::uint32_t rings = 8,
                                           std::uint32_t segments = 32);
} // namespace NS::Gfx
