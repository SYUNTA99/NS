#pragma once

#include "NSlib/Object/Object.h"
#include "NSlib/UI/WidgetSubObj.h"

namespace NS::Gfx
{
    struct RenderContext;
} // namespace NS::Gfx

namespace NS::Obj
{
    class Scene;

    //! @brief 体力の表示やメニューなど、画面に出す物の基底
    //! @details 寿命は開いた側が持ち、Open で登録し Close か破棄で外れる
    //! UI の段で開いた順に更新し、世界の後に DrawOrder 順で描く
    class UIActor : public Object
    {
    public:
        UIActor() noexcept = default;
        ~UIActor() noexcept override;
        //! この画面の Widget の木を持つ部品
        [[nodiscard]] NS::UI::WidgetSubObj* Widgets() noexcept { return &m_widgets; }
        NS_REFLECT_NONE(UIActor, Object)

        //! scene の画面の一覧へ入る。開いていれば何もしない
        void Open(Scene& scene);

        //! 画面の一覧から外れる。開いていなければ何もしない
        void Close() noexcept;

        //! 画面の一覧に入っているか
        [[nodiscard]] bool IsOpen() const noexcept { return m_scene != nullptr; }

        //! 開いている先の Scene。開いていなければ nullptr
        [[nodiscard]] Scene* OwningScene() const noexcept { return m_scene; }

        //! UI の段で 1 固定ステップに 1 回呼ばれる。シミュレーションが止まっている間は呼ばれない。既定は何もしない
        virtual void OnTick() {}

        //! @brief 世界と重ね描きの部品を描いた後の画面へ重ねて描く
        //! @details 既定は開いた先の UIDirector の UICanvas で木を描く。開いていなければ描かない
        virtual void OnRenderOverlay(const NS::Gfx::RenderContext& context);

        //! 描く順。小さいほど先に描き、後から描く物が上に重なる
        [[nodiscard]] virtual int DrawOrder() const noexcept { return 0; }

    private:
        NS::UI::WidgetSubObj m_widgets;
        Scene* m_scene = nullptr; // 開いている先、非所有
    };
} // namespace NS::Obj
