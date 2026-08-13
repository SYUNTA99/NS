#pragma once

// 線分と AABB / OBB / カプセルの枠線を溜めて、まとめて描くデバッグ描画

#include "Runtime/Core/Math.h"

namespace NS::Graphics
{
    class Renderer;
}

namespace NS::Graphics::DebugDraw
{
    //! @brief 2点間に線分を追加する
    //! @param[in] a 始点の座標
    //! @param[in] b 終点の座標
    //! @param[in] color 描画色
    void Line(const NS::Core::Vector3& a, const NS::Core::Vector3& b, const NS::Core::Color& color) noexcept;

    //! @brief AABB の枠線を追加する
    //! @param[in] box 描画するAABBデータ
    //! @param[in] color 描画色
    void AABB(const NS::Core::AABB& box, const NS::Core::Color& color) noexcept;

    //! @brief OBB の枠線を追加する
    //! @param[in] obb 描画するOBBデータ
    //! @param[in] color 描画色
    void OBB(const NS::Core::OBB& obb, const NS::Core::Color& color) noexcept;

    //! @brief カプセル形状の枠線を追加する
    //! @details 軸方向に伸びる円柱部分と、両端の半球の概形を描画する
    //! @param[in] base カプセル中心の座標
    //! @param[in] axis 中心から端の半球中心へ向かうベクトル（長さは halfHeight）
    //! @param[in] radius カプセルの半径
    //! @param[in] color 描画色
    void Capsule(const NS::Core::Vector3& base,
                 const NS::Core::Vector3& axis,
                 float radius,
                 const NS::Core::Color& color) noexcept;

    //! @brief 蓄積された図形群を一括で描画し、内部のバッファをクリアする
    //! @param[in,out] renderer コマンドを発行する描画システム
    //! @param[in] viewProjection ビュー・プロジェクション行列
    void Flush(Renderer& renderer, const NS::Core::Matrix& viewProjection) noexcept;

    //! バッファを破棄する
    void Clear() noexcept;

    //! 現在バッファに蓄積されている頂点の総数を返す
    [[nodiscard]] std::size_t VertexCount() noexcept;
} // namespace NS::Graphics::DebugDraw