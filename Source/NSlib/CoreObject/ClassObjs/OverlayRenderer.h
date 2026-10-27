#pragma once

#include "NSlib/CoreObject/Object.h"

namespace NS
{
    namespace Gfx
    {
        struct RenderContext;
    }
} // namespace NS

namespace NS
{
    //! シーンを描き終えた画面へ重ねて描く物。重ね描きの登録簿が受け取る型
    //! 部品は OverlayRenderer を継ぎ、持ち主の SceneActor が OnInit で InitOverlay を呼んで入れ、
    //! OnEndPlay で EndOverlay を呼んで外す。出る・消える時は ShowOverlay・HideOverlay で切り替える。
    //! 部品でない物はこれを直に継ぎ、自分で登録簿へ出し入れする
    class IOverlay
    {
    public:
        //! 重ね描きの順を返す
        [[nodiscard]] virtual int OverlayOrder() const noexcept { return 0; }

        //! 標準のシーンの描画パスの後に画面へ重ねて描く
        virtual void OnRenderOverlay(const Gfx::RenderContext& context) = 0;

    protected:
        ~IOverlay() = default;
    };

    //! 世界の描画後に重ね描きする部品の基底
    class OverlayRenderer : public Object, public IOverlay
    {
    public:
        NS_REFLECT_NONE(OverlayRenderer, Object)
    };
} // namespace NS
