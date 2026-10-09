#pragma once

#include "Game/Level/GoalParams.h"
#include "NSlib/Object/Actor.h"

namespace NS::Game::Level
{
    //! @brief 触れたらクリアになるゴール。金色の立方体と、範囲のセンサーを持つ
    //! @details 範囲がプレイヤーの体に重なったら MsgGoal を送る。クリアの流れは受け取ったプレイヤーと進行役が決める
    class Goal : public NS::Obj::Actor
    {
    public:
        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        NS_REFLECT_NONE(Goal, NS::Obj::Actor)

        //! 範囲に入ったプレイヤーの体へ MsgGoal を送る
        void AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other) override;

    protected:
        void Init() override;

    private:
        GoalParams* m_params = nullptr;
    };
} // namespace NS::Game::Level
