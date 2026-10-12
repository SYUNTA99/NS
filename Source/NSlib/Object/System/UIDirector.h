#pragma once

#include "NSlib/Object/ITickable.h"
#include "NSlib/Object/System/SceneSystem.h"

#include <vector>

namespace NS::Obj
{
    class UIActor;

    //! @brief 開いている画面を束ね、UI の段で動かし、描く順を返す
    //! @details 画面は所有しない。寿命は開いた側が持ち、UIActor の Open と Close が出し入れする
    //! Scene がメンバーで持つので、配置物を組み直しても開いた画面は残る
    class UIDirector final : public SceneSystem, public ITickable
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

    private:
        std::vector<UIActor*> m_screens; // 開いた順、非所有
    };
} // namespace NS::Obj
