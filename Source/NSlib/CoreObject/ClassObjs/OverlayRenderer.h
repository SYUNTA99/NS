#pragma once

#include "NSlib/CoreObject/SceneActor.h"
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
        virtual void OnRenderOverlay(const Gfx::RenderContext& context) = 0;

    protected:
        ~IOverlay() = default;
    };

    //! 世界の描画後に重ね描きする部品の基底
    class OverlayRenderer : public Object, public IOverlay
    {
    public:
        explicit OverlayRenderer(SceneActor* owner) noexcept : m_owner(owner) {}

        //! 持ち主が出ている間だけ描く。自分の値だけ見ると、持ち主を外しても描き続ける
        //! 持ち主の無い物は出ていると見る
        [[nodiscard]] bool IsOverlayVisible() const noexcept override
        {
            if (m_owner == nullptr)
            {
                return true;
            }
            return m_owner->IsAppeared();
        }

        NS_REFLECT_NONE(OverlayRenderer, Object)

    private:
        SceneActor* m_owner = nullptr; // 持ち主 (非所有)
    };
} // namespace NS
