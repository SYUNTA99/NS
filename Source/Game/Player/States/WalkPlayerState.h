#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL::Player
{
        class WalkPlayerState final : public PlayerState<WalkPlayerState>
        {
        public:
            void OnStep(::PlayerActor* player, float dt) override;
        };
} // namespace GL::Player
