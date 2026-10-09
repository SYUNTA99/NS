#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace GL::Player
{
    //! ブレーキ。止まるまで入力の向きへ加速しない。移る先は落下と立ち
    class BrakePlayerState final : public PlayerState<BrakePlayerState>
    {
    public:
        void OnStep(::Player& player, float dt) override;
    };
} // namespace GL::Player
