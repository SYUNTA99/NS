#pragma once

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Scene/SceneJson.h"

namespace NS::Game::Level
{
    //! @brief 触れたらクリアになるゴール。金色の立方体と、触れた判定の GoalComponent を持つ
    class Goal : public NS::Obj::Actor
    {
    public:
        Goal() noexcept;

        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        [[nodiscard]] const char* ClassName() const noexcept override { return "Goal"; }
    };

    //! ゴールの配置物の JSON か
    [[nodiscard]] bool IsGoalObject(const nlohmann::json& object) noexcept;
} // namespace NS::Game::Level
