#pragma once

#include "Game/Entity/EntityState.h"

class Player;

namespace NS::Game::Player
{
    class PlayerComponent;

    //! @brief 自機の 1 状態
    //! @details 中身は PlayerComponent の能力呼びの列と遷移だけ。条件判定を書くと同じ判断が状態の数だけ増える
    template <typename TState> using PlayerState = NS::Game::Entity::EntityState<TState, ::Player>;
} // namespace NS::Game::Player
