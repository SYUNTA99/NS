#pragma once

#include "NSlib/Object/SubObject.h"

namespace NS::Gfx
{
    struct RenderContext;
} // namespace NS::Gfx

namespace NS::Obj
{
    //! @brief シーンを描き終えた画面へ重ねて描く物。重ね描きの登録簿が受け取る型
    //! @details 部品は OverlayRenderer を継ぎ、持ち主の SceneActor が OnStart で登録簿へ入れて OnEndPlay で外す。
    //! 部品でない物はこれを直に継ぎ、自分で登録簿へ出し入れする
    class IOverlay
    {
    public:
        //! @brief 重ね描きの順を返す
        [[nodiscard]] virtual int OverlayOrder() const noexcept { return 0; }

        //! 今描く場合 true、飛ばす場合は false
        [[nodiscard]] virtual bool IsOverlayVisible() const noexcept { return true; }

        //! 標準のシーンの描画パスの後に画面へ重ねて描く
        virtual void OnRenderOverlay(const NS::Gfx::RenderContext& context) = 0;

    protected:
        ~IOverlay() = default;
    };

    //! 世界の描画後に重ね描きする部品の基底
    class OverlayRenderer : public SubObject, public IOverlay
    {
    public:
        OverlayRenderer() noexcept = default;

        //! 更新・当たりと同じ IsActive で切る。自分の値だけ見ると親を寝かせても描き続ける
        [[nodiscard]] bool IsOverlayVisible() const noexcept override { return IsActive(); }

        NS_REFLECT_NONE(OverlayRenderer, SubObject)
    };
} // namespace NS::Obj
