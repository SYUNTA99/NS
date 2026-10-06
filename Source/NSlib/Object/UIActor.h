#pragma once

#include "NSlib/Object/ActorBase.h"
#include "NSlib/Object/ITickable.h"
#include "NSlib/UI/UISystem.h"

namespace NS::Gfx
{
    struct RenderContext;
} // namespace NS::Gfx

namespace NS::Obj
{
    //! @brief 画面に出す物の基底。暗転・体力の表示・メニューなど
    //! @details 世界の座標と当たりを持たず、世界を描いた後の画面へ重ねて描く。オデッセイの LayoutActor に当たる
    //! 寿命は開いた側が持つ。Open でシーンの画面の一覧と UI の段の登録物へ入り、Close か破棄で外れる
    //! 更新は UI の段の Actor の後で、開いた順に OnTick が呼ばれる。描画は世界と重ね描きの部品の後で、
    //! 描く順は開いた順でなく DrawOrder が決める
    class UIActor : public ActorBase, public ITickable
    {
    public:
        UIActor() noexcept = default;
        ~UIActor() noexcept override;
        //! この画面が持つ UI の部品の一覧
        [[nodiscard]] NS::UI::UISystem& Widgets() noexcept { return m_widgets; }
        NS_REFLECT_NONE(UIActor, ActorBase)

        //! scene の画面の一覧へ入る。開いていれば何もしない
        void Open(Scene& scene);

        //! 画面の一覧から外れる。開いていなければ何もしない
        void Close() noexcept;

        //! 画面の一覧に入っているか
        [[nodiscard]] bool IsOpen() const noexcept { return IsAlive() && OwningScene() != nullptr; }

        //! UI の段で 1 固定ステップに 1 回呼ばれる。シミュレーションが止まっている間は呼ばれない。既定は何もしない
        void OnTick() override {}

        //! 世界と重ね描きの部品を描いた後の画面へ重ねて描く
        virtual void OnRenderOverlay(const NS::Gfx::RenderContext& context);

        //! 描く順。小さいほど先に描き、後から描く物が上に重なる
        [[nodiscard]] virtual int DrawOrder() const noexcept { return 0; }

    protected:
        void OnAppear() override;
        void OnKill() noexcept override;

    private:
        NS::UI::UISystem m_widgets;
    };
} // namespace NS::Obj
