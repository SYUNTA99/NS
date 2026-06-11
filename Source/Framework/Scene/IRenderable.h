#pragma once

/// @file IRenderable.h
/// @brief NS::Scene::IRenderable — 描画機能を持つ Component が多重継承する基底インターフェース
///
/// 描画責務を持つ Component (MeshRendererComponent 等) は `IRenderable` を多重継承して
/// `Draw(const RenderContext&)` を実装する。`OnStart` で
/// `Owner()->OwningScene()->RegisterRenderable(this)` を呼んで自己登録し、`OnEndPlay` で解除する
/// SceneBase が render iteration を握るため、Player.cpp / Block.cpp は render を 1 行も書かない

namespace NS::Scene
{
    struct RenderContext;

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
    };

} // namespace NS::Scene
