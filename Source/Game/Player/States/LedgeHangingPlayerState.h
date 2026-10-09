#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace GL::Player
{
    //! ぶら下がり。移る先はよじ登りと落下
    class LedgeHangingPlayerState final : public PlayerState<LedgeHangingPlayerState>
    {
    public:
        void OnStep(::Player& player, float dt) override;
    };
} // namespace GL::Player
