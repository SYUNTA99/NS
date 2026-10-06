#pragma once

#include "NSlib/Object/Component.h"

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
    class OverlayRenderer : public Component
    {
    public:
        OverlayRenderer() noexcept = default;
        //! @brief 重ね描きの順を返す
        //! @return 小さい方から先に描く。同じ値は先に登録した方が先。既定は 0
        [[nodiscard]] virtual int OverlayOrder() const noexcept { return 0; }

        //! 標準の world 描画パスの後に画面へ重ねて描く
        virtual void OnRenderOverlay(const NS::Gfx::RenderContext& context) = 0;

        //! 所属 scene の重ね描きの登録簿へ自分を入れる。派生で上書きするなら基底のこれを呼ぶ
        void OnAppear() override { OverlayRenderer::OnStart(); }
        void OnKill() noexcept override { OverlayRenderer::OnEndPlay(); }
        void OnStart() override;
        //! 所属 scene の重ね描きの登録簿から自分を外す。派生で上書きするなら基底のこれを呼ぶ
        void OnEndPlay() override;

        NS_REFLECT_NONE(OverlayRenderer, Component)
    };
} // namespace NS::Obj
