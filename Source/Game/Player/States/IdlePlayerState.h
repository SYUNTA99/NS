#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! 立ち。移る先は落下と走り
    class IdlePlayerState final : public PlayerState
    {
    public:
        static constexpr const char* k_Name = "Idle";

        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }

        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
