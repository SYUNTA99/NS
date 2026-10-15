#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL::Player
{
    class LedgeHangingPlayerState final : public PlayerState<LedgeHangingPlayerState>
    {
    public:
        void OnStep(::PlayerActor& player, float dt) override;
    };
} // namespace GL::Player
