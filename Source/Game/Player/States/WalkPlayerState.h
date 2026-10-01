#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace NS::Game::Player
{
    //! 走り。落下・ブレーキ・立ちへ移る
    class WalkPlayerState final : public PlayerState<WalkPlayerState>
    {
    public:
        void OnStep(::Player& player, float dt) override;
    };
} // namespace NS::Game::Player
