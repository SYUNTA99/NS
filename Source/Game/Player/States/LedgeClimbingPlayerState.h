#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! よじ登り。登り切ったフレームに立ちへ移る
    class LedgeClimbingPlayerState final : public PlayerState
    {
    public:
        static constexpr const char* k_Name = "LedgeClimbing";

        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }

        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
