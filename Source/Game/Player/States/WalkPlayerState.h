#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! 走り。移る先は落下・ブレーキ・立ち
    class WalkPlayerState final : public PlayerState
    {
    public:
        static constexpr const char* k_Name = "Walk";

        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }

        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
