#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace NS::Game::Player
{
    //! 突進。移る先は走りと落下
    class BodySlamPlayerState final : public PlayerState<BodySlamPlayerState>
    {
    public:
        void OnStep(::Player& player, float dt) override;
    };
} // namespace NS::Game::Player
