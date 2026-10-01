#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace NS::Game::Player
{
    //! 反動。移る先は立ち・ぶら下がり・突進
    class ReboundPlayerState final : public PlayerState<ReboundPlayerState>
    {
    public:
        void OnEnter(::Player& player) override;
        void OnStep(::Player&, float) override {}

    private:
        NS::Core::Coroutine Run(::Player& player);
    };
} // namespace NS::Game::Player
