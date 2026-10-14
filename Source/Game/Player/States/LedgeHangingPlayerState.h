#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace GL::Player
{
    class LedgeHangingPlayerState final : public PlayerState<LedgeHangingPlayerState>
    {
    public:
        void OnStep(::Player& player, float dt) override;
    };
} // namespace GL::Player
