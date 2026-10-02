#pragma once

#include "Game/Entity/EntityState.h"

class Player;

namespace NS::Game::Player
{
    //! @brief 自機の 1 状態
    //! @details 中身は Player の技の呼びの列と遷移だけ。遷移の条件は PlayerJudges
    //! の判定を呼ぶ。状態の中に条件を書くと同じ判断が状態の数だけ増える
    template <typename TState> using PlayerState = NS::Game::Entity::EntityState<TState, ::Player>;
} // namespace NS::Game::Player
