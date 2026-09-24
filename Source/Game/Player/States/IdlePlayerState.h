#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! 立ち。落下と走りへ移る
    class IdlePlayerState final : public PlayerState<IdlePlayerState>
    {
    public:
        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
