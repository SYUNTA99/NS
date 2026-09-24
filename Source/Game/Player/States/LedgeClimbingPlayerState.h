#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! よじ登り。登り切ったフレームに立ちへ移る
    class LedgeClimbingPlayerState final : public PlayerState<LedgeClimbingPlayerState>
    {
    public:
        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
