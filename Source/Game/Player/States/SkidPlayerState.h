#pragma once

#include "Game/Player/PlayerState.h"

class Player;

namespace GL::Player
{
    //! 外れの反動の着地から、こすって止まる。止まりきるまで操作を受けない。移る先は立ちと落下
    class SkidPlayerState final : public PlayerState<SkidPlayerState>
    {
    public:
        void OnEnter(::Player& player) override;
        void OnStep(::Player& player, float dt) override;
    };
} // namespace GL::Player
