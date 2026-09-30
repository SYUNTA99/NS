#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    //! @brief ゴールの Actor が持つ、触れた判定の部品
    //! @details プレイヤーとの中心距離を LateUpdate 帯で自分で判定し、触れたらフラグを立てて保持する
    //! フラグを読んでどう応答するかは Finisher が決める。フラグは走行のやり直しと編集復帰で戻される
    class GoalComponent : public NS::Obj::Component
    {
    public:
        GoalComponent() noexcept;

        void OnUpdate() override;

        [[nodiscard]] bool Reached() const noexcept { return m_reached; }

        //! フラグを戻す。走行のやり直し (respawner) と編集へ戻る時 (エディタ) に呼ばれる
        void ResetReached() noexcept { m_reached = false; }

        NS_REFLECT_BEGIN(GoalComponent, NS::Obj::Component)
        NS_REFLECT_FIELD(m_radius, "半径")
        NS_REFLECT_END()

    private:
        float m_radius = 0.9f;  // 「触れた」とみなす player 中心からの距離 (m)
        bool m_reached = false; // 触れたら立つ。保存しない
    };
} // namespace NS::Game::Level
