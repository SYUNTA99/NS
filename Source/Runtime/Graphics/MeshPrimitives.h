#pragma once

#include "Runtime/Graphics/StaticMesh.h"
#include "Runtime/Math/Math.h"

namespace NS::Graphics
{
    //! @brief 基本プリミティブ生成処理が返す、CPU側のジオメトリデータ。
    //! @details 描画用メッシュの構築パラメータとして使用する。
    //! @note メッシュの構築処理内でグラフィックスメモリへ転送されるため、構築完了後は破棄してよい。
    struct MeshGeometry
    {
        std::vector<StaticVertex> vertices; // 頂点データ
        std::vector<std::uint32_t> indices; // index データ
        NS::Math::AABB bounds{};            // 頂点構築時に広げた軸並行境界
        bool hasBounds = false;             // 有効なら StaticMesh がこれを使い、無ければ頂点から算出
    };

    //! @brief 立方体プリミティブのジオメトリを生成する
    //! @details 各面に独立した法線を持つ（24頂点、36インデックス）
    //! @param extents 幅・高さ・奥行きのサイズ
    //! @note 内部のメモリ確保に失敗した場合は例外が送出される可能性がある
    [[nodiscard]] MeshGeometry MakeCube(const NS::Math::Vector3& extents);

    //! @brief 平面プリミティブのジオメトリを生成する（4頂点、6インデックス）
    //! @param extents 幅・高さ・奥行きのサイズ
    [[nodiscard]] MeshGeometry MakePlane(const NS::Math::Vector2& extents);

    //! @brief Z軸プラス方向へ上昇するスロープ（坂）のジオメトリを生成する（18頂点、24インデックス）
    //! @param angleDegrees スロープの傾斜角
    //! @param extents 幅・高さ・奥行きのサイズ
    [[nodiscard]] MeshGeometry MakeSlope(float angleDegrees, const NS::Math::Vector3& extents);

    //! @brief 原点中心の球のジオメトリを生成する
    //! @param radius 球の半径
    //! @param rings 上下方向の分割数。2 未満は 2 に切り上げる
    //! @param segments 周方向の分割数。3 未満は 3 に切り上げる
    [[nodiscard]] MeshGeometry MakeSphere(float radius, std::uint32_t rings = 16, std::uint32_t segments = 32);
} // namespace NS::Graphics
