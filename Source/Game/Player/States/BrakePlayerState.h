#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL::Player
{
        //! 止まるまで入力方向へ加速しないブレーキ
        class BrakePlayerState final : public PlayerState<BrakePlayerState>
        {
        public:
            void OnStep(::PlayerActor* player, float dt) override;
        };
} // namespace GL::Player
