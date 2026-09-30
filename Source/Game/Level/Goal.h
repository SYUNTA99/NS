#pragma once

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Scene/SceneJson.h"

namespace NS::Game::Level
{
    //! @brief 触れたらクリアになるゴール。金色の立方体と、範囲のセンサーを持つ
    //! @details 範囲がプレイヤーの体に重なったら MsgGoal を送る。クリアの流れは受け取ったプレイヤーと進行役が決める
    class Goal : public NS::Obj::Actor
    {
    public:
        Goal() noexcept;

        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        [[nodiscard]] const char* ClassName() const noexcept override { return "Goal"; }

        //! 範囲に入ったプレイヤーの体へ MsgGoal を送る
        void AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other) override;
    };

    //! ゴールの配置物の JSON か
    [[nodiscard]] bool IsGoalObject(const nlohmann::json& object) noexcept;
} // namespace NS::Game::Level
