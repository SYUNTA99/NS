#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL
{
    namespace Player
    {
        class BodySlamPlayerState final : public PlayerState<BodySlamPlayerState>
        {
        public:
            void OnStep(::PlayerActor& player, float dt) override;
        };
    } // namespace Player
} // namespace GL
