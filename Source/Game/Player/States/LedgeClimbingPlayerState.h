#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace NS::Game::Player
{
    //! よじ登り。登り切ったフレームに立ちへ移る
    class LedgeClimbingPlayerState final : public PlayerState<LedgeClimbingPlayerState>
    {
    public:
        void OnEnter(::Player& player) override;
        void OnStep(::Player&, float) override {}

    private:
        NS::Core::Coroutine Run(::Player& player);
    };
} // namespace NS::Game::Player
