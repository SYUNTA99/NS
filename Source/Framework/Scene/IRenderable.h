#pragma once

/// @file i_renderable.h
/// @brief NS::Scene::IRenderable — Component が描画機能を mix-in するための interface。
///
/// 描画責務を持つ Component (MeshComponent / DebugDrawComponent 等) は `IRenderable` を
/// 多重継承して `Draw(const RenderContext&)` を実装する。`OnStart` で
/// `Owner()->OwningScene()->RegisterRenderable(this)` を呼んで自己登録し、`OnEndPlay` で
/// 解除する。RootScene 実装が render iteration を握るため、Player.cpp / Block.cpp は
/// render を 1 行も書かない (UE5/Unity 流儀)。

namespace NS::Scene
{
    struct RenderContext;

    /// 描画 callback の interface。Component との多重継承を想定。
    class IRenderable
    {
    public:
        IRenderable() noexcept = default;
        virtual ~IRenderable() noexcept = default;

        IRenderable(const IRenderable&) = delete;
        IRenderable& operator=(const IRenderable&) = delete;
        IRenderable(IRenderable&&) = delete;
        IRenderable& operator=(IRenderable&&) = delete;

        /// RootScene::OnRender から呼ばれる。Alpha 補間後の transform を ctx.alpha 経由で取得。
        virtual void Draw(const RenderContext& context) = 0;
    };

} // namespace NS::Scene
