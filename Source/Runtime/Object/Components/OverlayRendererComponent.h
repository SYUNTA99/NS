#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Graphics
{
    struct RenderContext;
} // namespace NS::Graphics

namespace NS::Object
{
    //! @brief world を描き終えた画面へ重ねて描く Component の共通基底
    //! @details 重ね描きの口を持つのはこの型の派生だけ。 何をどう描くかはゲーム側が決める
    //! 抽象基底なので TypeRegistry には登録しない
    //! 依存: NS::Graphics::RenderContext
    class OverlayRendererComponent : public Component
    {
    public:
        //! 中間基底を挟むと派生から Component の初期化子を書けないので priority をここで受けて渡す
        explicit OverlayRendererComponent(int priority = TickPriority::Update) noexcept
            : Component(priority)
        {}

        //! 標準の world 描画パスの後に画面へ重ねて描く
        virtual void OnRenderOverlay(const NS::Graphics::RenderContext& context) = 0;

        NS_REFLECT_NONE(OverlayRendererComponent, Component)
    };
} // namespace NS::Object
