#pragma once

#include "Runtime/Object/ActorBase.h"

namespace NS::Gfx
{
    struct RenderContext;
} // namespace NS::Gfx

namespace NS::Obj
{
    //! @brief 画面に出す物の基底。暗転・体力の表示・メニューなど
    //! @details 世界の座標と当たりを持たず、世界を描いた後の画面へ重ねて描く。オデッセイの LayoutActor に当たる
    //! 寿命は開いた側が持つ。Open でシーンの画面の一覧へ入り、Close か破棄で外れる
    //! 更新は世界の更新とカメラの後、UI の段で 1 固定ステップに 1 回。描画は世界と重ね描きの部品の後
    class UIActor : public ActorBase
    {
    public:
        UIActor() noexcept = default;
        ~UIActor() noexcept override;

        //! scene の画面の一覧へ入る。開いていれば何もしない
        void Open(Scene& scene);

        //! 画面の一覧から外れる。開いていなければ何もしない
        void Close() noexcept;

        //! 画面の一覧に入っているか
        [[nodiscard]] bool IsOpen() const noexcept { return OwningScene() != nullptr; }

        //! UI の段で 1 固定ステップに 1 回呼ばれる。シミュレーションが止まっている間は呼ばれない
        virtual void OnUpdate() {}

        //! 世界と重ね描きの部品を描いた後の画面へ重ねて描く
        virtual void OnRenderOverlay(const NS::Gfx::RenderContext& context) = 0;

        //! 描く順。小さいほど先に描き、後から描く物が上に重なる
        [[nodiscard]] virtual int DrawOrder() const noexcept { return 0; }
    };
} // namespace NS::Obj
