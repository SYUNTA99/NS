#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace GL::Player
{
    class ReboundPlayerState final : public PlayerState<ReboundPlayerState>
    {
    public:
        void OnEnter(::Player& player) override;
        void OnStep(::Player&, float) override {}

    private:
        NS::Coroutine Run(::Player& player);
    };
} // namespace GL::Player
