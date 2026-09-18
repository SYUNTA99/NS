#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! ぶら下がり。移る先はよじ登りと落下
    class LedgeHangingPlayerState final : public PlayerState
    {
    public:
        static constexpr const char* k_Name = "LedgeHanging";

        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }

        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
