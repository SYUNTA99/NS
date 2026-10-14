#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace GL::Player
{
    //! 止まるまで入力方向へ加速しないブレーキ
    class BrakePlayerState final : public PlayerState<BrakePlayerState>
    {
    public:
        void OnStep(::Player& player, float dt) override;
    };
} // namespace GL::Player
