#pragma once

#include "Game/Player/PlayerState.h"

namespace NS::Game::Player
{
    //! ブレーキ。止まるまで入力の向きへ加速しない。移る先は落下と立ち
    class BrakePlayerState final : public PlayerState<BrakePlayerState>
    {
    public:
        static constexpr const char* k_Name = "Brake";

        void OnStep(PlayerComponent& player, float dt) override;
    };
} // namespace NS::Game::Player
