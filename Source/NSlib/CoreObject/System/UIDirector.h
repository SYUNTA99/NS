#pragma once

#include "NSlib/CoreObject/System/SceneSystem.h"
#include "NSlib/UI/UICanvas.h"

#include <vector>

namespace NS::Gfx
{
        struct RenderContext;
} // namespace NS::Gfx

namespace NS
{
    class UIActor;

    //! @brief 開いている画面を束ね、UI の段で動かし、描く順に描く
    //! @details 画面は所有しない。寿命は開いた側が持ち、UIActor の Open と Close が出し入れする
    //! Scene がメンバーで持つので、アクターを組み直しても開いた画面は残る
    class UIDirector final : public SceneSystem
    {
    public:
        //! 画面を一覧の末尾へ入れる。入っていれば何もしない
        void Add(UIActor& screen);
        //! 画面を一覧から外す。入っていなければ何もしない
        void Remove(UIActor& screen) noexcept;

        //! @brief 開いている画面を開いた順に 1 回ずつ動かす
        //! @details 最中に開いた画面は次の歩から動き、最中に閉じた画面は飛ばす
        void OnTick() override;

        //! @brief 描く順に並べた画面を返す
        //! @return DrawOrder の昇順で同じ値は開いた順の一覧、非所有
        [[nodiscard]] std::vector<UIActor*> DrawOrdered() const;

        //! @brief 描画先の大きさから UICanvas を作って控え、画面を描く順に描く
        //! @details device がなければ UICanvas は既定の値を使い、画面の描画処理は呼ぶ
        void Draw(const Gfx::RenderContext& context);

        //! 直近の Draw で控えた拡縮と全画面の矩形。画面が描く時に借りる
        [[nodiscard]] const UI::UICanvas& Canvas() const noexcept { return m_canvas; }

    private:
        std::vector<UIActor*> m_screens; // 開いた順、非所有
        UI::UICanvas m_canvas{};     // 直近の Draw の描画先で作った値
    };
} // namespace NS
