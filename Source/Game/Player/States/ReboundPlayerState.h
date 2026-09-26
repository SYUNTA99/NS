#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! 反動。移る先は立ち・ぶら下がり・突進
    class ReboundPlayerState final : public PlayerState<ReboundPlayerState>
    {
    public:
        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
