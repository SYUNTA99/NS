#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace GL::Player
{
    //! よじ登り。登り切ったフレームに立ちへ移る
    class LedgeClimbingPlayerState final : public PlayerState<LedgeClimbingPlayerState>
    {
    public:
        void OnEnter(::Player& player) override;
        void OnStep(::Player&, float) override {}

    private:
        NS::Coroutine Run(::Player& player);
    };
} // namespace GL::Player
