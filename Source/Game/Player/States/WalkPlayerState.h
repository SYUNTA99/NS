#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace GL::Player
{
    class WalkPlayerState final : public PlayerState<WalkPlayerState>
    {
    public:
        void OnStep(::Player& player, float dt) override;
    };
} // namespace GL::Player
