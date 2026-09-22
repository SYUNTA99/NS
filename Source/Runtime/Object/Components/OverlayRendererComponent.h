#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Gfx
{
    struct RenderContext;
} // namespace NS::Gfx

namespace NS::Obj
{
    //! @brief world を描き終えた画面へ重ねて描く Component の共通基底
    //! @details 重ね描きの口を持つのはこの型の派生だけ。何をどう描くかはゲーム側が決める
    //! 抽象基底なので TypeRegistry には登録しない
    //! 依存: NS::Gfx::RenderContext
    class OverlayRendererComponent : public Component
    {
    public:
        //! 中間基底を挟むと派生から Component の初期化子を書けないので priority をここで受けて渡す
        explicit OverlayRendererComponent(int priority = TickPriority::Update) noexcept : Component(priority) {}

        //! 標準の world 描画パスの後に画面へ重ねて描く
        virtual void OnRenderOverlay(const NS::Gfx::RenderContext& context) = 0;

        //! 所属 scene の重ね描きの登録簿へ自分を入れる。派生で上書きするなら基底のこれを呼ぶ
        void OnStart() override;
        //! 所属 scene の重ね描きの登録簿から自分を外す。派生で上書きするなら基底のこれを呼ぶ
        void OnEndPlay() override;

        NS_REFLECT_NONE(OverlayRendererComponent, Component)
    };
} // namespace NS::Obj
