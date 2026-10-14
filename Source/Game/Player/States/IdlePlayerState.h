#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace GL::Player
{
    class IdlePlayerState final : public PlayerState<IdlePlayerState>
    {
    public:
        void OnStep(::Player& player, float dt) override;
    };
} // namespace GL::Player
