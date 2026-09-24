#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! ぶら下がり。移る先はよじ登りと落下
    class LedgeHangingPlayerState final : public PlayerState<LedgeHangingPlayerState>
    {
    public:
        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
