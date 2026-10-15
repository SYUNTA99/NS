#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL::Player
{
    class ReboundPlayerState final : public PlayerState<ReboundPlayerState>
    {
    public:
        void OnEnter(::PlayerActor& player) override;
        void OnStep(::PlayerActor&, float) override {}

    private:
        NS::Coroutine Run(::PlayerActor& player);
    };
} // namespace GL::Player
