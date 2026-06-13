#pragma once

/// @file IRenderable.h
/// @brief NS::Scene::IRenderable — 描画機能を持つ Component が多重継承する基底インターフェース
///
/// 描画責務を持つ Component (MeshRendererComponent 等) は `IRenderable` を多重継承して
/// `Draw(const RenderContext&)` を実装する。`OnStart` で
/// `Owner()->OwningScene()->RegisterRenderable(this)` を呼んで自己登録し、`OnEndPlay` で解除する
/// SceneBase が render iteration を握るため、Player.cpp / Block.cpp は render を 1 行も書かない

#include "Framework/Math/Math.h"

namespace NS::Scene
{
    struct RenderContext;

    /// 描画バケット。Opaque は登録順、Transparent はカメラ距離で back-to-front ソートされる
    enum class RenderBucket
    {
        Opaque,
        Transparent
    };

    /// 描画コールバックを持つ基底。Component との多重継承を想定
    class IRenderable
    {
    public:
        IRenderable() noexcept = default;
        virtual ~IRenderable() noexcept = default;

        IRenderable(const IRenderable&) = delete;
        IRenderable& operator=(const IRenderable&) = delete;
        IRenderable(IRenderable&&) = delete;
        IRenderable& operator=(IRenderable&&) = delete;

        /// SceneBase::OnRender から呼ばれる。Alpha 補間後の transform を context.alpha 経由で取得
        virtual void Draw(const RenderContext& context) = 0;

        /// 自分の描画バケット。既定は Opaque (透明を持たない既存 Renderable は無改変で従来どおり)
        [[nodiscard]] virtual RenderBucket Bucket() const noexcept { return RenderBucket::Opaque; }

        /// 半透明ソート用のワールド空間中心座標。既定は原点 (Opaque は未使用)
        [[nodiscard]] virtual NS::Math::Vector3 SortCenter() const noexcept { return {}; }

        /// 半透明ソートのタイブレーク優先度。距離同値時に小さいほど先。既定 0
        [[nodiscard]] virtual int SortPriority() const noexcept { return 0; }
    };

} // namespace NS::Scene
