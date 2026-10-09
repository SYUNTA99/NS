#pragma once

#include "NSlib/Object/SubObject.h"

namespace NS::Gfx
{
    struct RenderContext;
} // namespace NS::Gfx

namespace NS::Obj
{
    //! @brief world を描き終えた画面へ重ねて描く物。重ね描きの登録簿が受け取る型
    //! @details 部品は OverlayRenderer を継ぐ。部品でない物はこれを直に継ぎ、自分で登録簿へ出し入れする
    class IOverlay
    {
    public:
        //! @brief 重ね描きの順を返す
        //! @return 小さい方から先に描く。同じ値は先に登録した方が先。既定は 0
        [[nodiscard]] virtual int OverlayOrder() const noexcept { return 0; }

        //! 今描く場合 true、飛ばす場合は false
        [[nodiscard]] virtual bool IsOverlayVisible() const noexcept { return true; }

        //! 標準の world 描画パスの後に画面へ重ねて描く
        virtual void OnRenderOverlay(const NS::Gfx::RenderContext& context) = 0;

    protected:
        ~IOverlay() = default;
    };

    //! @brief world を描き終えた画面へ重ねて描く SubObject の共通基底
    //! @details 何をどう描くかはゲーム側が決める
    //! 抽象基底なので TypeRegistry には登録しない
    class OverlayRenderer : public SubObject, public IOverlay
    {
    public:
        OverlayRenderer() noexcept = default;

        //! 更新・当たりと同じ IsActive で切る。自分の値だけ見ると親を寝かせても描き続ける
        [[nodiscard]] bool IsOverlayVisible() const noexcept override { return IsActive(); }

        //! 所属 scene の重ね描きの登録簿へ自分を入れる。派生で上書きするなら基底のこれを呼ぶ
        void OnAppear() override { OverlayRenderer::OnStart(); }
        void OnKill() noexcept override { OverlayRenderer::OnEndPlay(); }
        void OnStart() override;
        //! 所属 scene の重ね描きの登録簿から自分を外す。派生で上書きするなら基底のこれを呼ぶ
        void OnEndPlay() override;

        NS_REFLECT_NONE(OverlayRenderer, SubObject)
    };
} // namespace NS::Obj
