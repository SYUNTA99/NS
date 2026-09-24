#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! 落下。着地したフレームに立ちへ移る
    class FallPlayerState final : public PlayerState<FallPlayerState>
    {
    public:
        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
