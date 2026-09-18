#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! 落下。着地したフレームに立ちへ移る
    class FallPlayerState final : public PlayerState
    {
    public:
        static constexpr const char* k_Name = "Fall";

        [[nodiscard]] const char* Name() const noexcept override { return k_Name; }

        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
