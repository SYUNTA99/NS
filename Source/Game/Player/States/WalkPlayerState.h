#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! 走り。落下・ブレーキ・立ちへ移る
    class WalkPlayerState final : public PlayerState<WalkPlayerState>
    {
    public:
        static constexpr const char* k_Name = "Walk";

        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
