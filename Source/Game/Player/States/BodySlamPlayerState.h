#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! 突進。移る先は走りと落下
    class BodySlamPlayerState final : public PlayerState<BodySlamPlayerState>
    {
    public:
        static constexpr const char* k_Name = "BodySlam";

        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
