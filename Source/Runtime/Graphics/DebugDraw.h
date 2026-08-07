#pragma once

// 線分や各種バウンディングボリューム（AABB、OBB、カプセル）の形状を蓄積し、一括で描画コマンドを発行するためのデバッグ描画機能。

#include "Runtime/Core/Math.h"

namespace NS::Graphics
{
    class Renderer;
}

namespace NS::Graphics::DebugDraw
{
    //! @brief 2点間に線分を追加する
    //! @param a 始点の座標
    //! @param b 終点の座標
    //! @param color 描画色
    void Line(const NS::Core::Vector3& a, const NS::Core::Vector3& b, const NS::Core::Color& color) noexcept;

    //! @brief 軸に平行な境界ボックス（AABB）の枠線を追加する
    //! @param box 描画するAABBデータ
    //! @param color 描画色
    void AABB(const NS::Core::AABB& box, const NS::Core::Color& color) noexcept;

    //! @brief 有向境界ボックス（OBB）の枠線を追加する
    //! @details 回転を伴う境界ボックスを描画する
    //! @param obb 描画するOBBデータ
    //! @param color 描画色
    void OBB(const NS::Core::OBB& obb, const NS::Core::Color& color) noexcept;

    //! @brief カプセル形状の枠線を追加する
    //! @details 軸方向に伸びる円柱部分と、両端の半球の概形を描画する
    //! @param base カプセル中心の座標
    //! @param axis 中心から端の半球中心へ向かうベクトル（長さは halfHeight）
    //! @param radius カプセルの半径
    //! @param color 描画色
    void Capsule(const NS::Core::Vector3& base,
                 const NS::Core::Vector3& axis,
                 float radius,
                 const NS::Core::Color& color) noexcept;

    //! @brief 蓄積された図形群を一括で描画し、内部のバッファをクリアする
    //! @param renderer コマンドを発行する描画システム
    //! @param viewProjection ビュー・プロジェクション行列
    void Flush(Renderer& renderer, const NS::Core::Matrix& viewProjection) noexcept;

    //! バッファを破棄する
    void Clear() noexcept;

    //! 現在バッファに蓄積されている頂点の総数を返す。
    [[nodiscard]] std::size_t VertexCount() noexcept;
} // namespace NS::Graphics::DebugDraw