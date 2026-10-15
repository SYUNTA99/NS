#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL
{
    namespace Player
    {
        class IdlePlayerState final : public PlayerState<IdlePlayerState>
        {
        public:
            void OnStep(::PlayerActor& player, float dt) override;
        };
    } // namespace Player
} // namespace GL
