#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace NS::Game::Player
{
    //! ぶら下がり。移る先はよじ登りと落下
    class LedgeHangingPlayerState final : public PlayerState<LedgeHangingPlayerState>
    {
    public:
        void OnStep(::Player& player, float dt) override;
    };
} // namespace NS::Game::Player
