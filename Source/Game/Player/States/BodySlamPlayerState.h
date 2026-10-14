#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace GL::Player
{
    class BodySlamPlayerState final : public PlayerState<BodySlamPlayerState>
    {
    public:
        void OnStep(::Player& player, float dt) override;
    };
} // namespace GL::Player
