#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! 立ち。落下と走りへ移る
    class IdlePlayerState final : public PlayerState<IdlePlayerState>
    {
    public:
        static constexpr const char* k_Name = "Idle";

        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
