#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL
{
    namespace Player
    {
        //! よじ登り。登り切ったフレームに立ちへ移る
        class LedgeClimbingPlayerState final : public PlayerState<LedgeClimbingPlayerState>
        {
        public:
            void OnEnter(::PlayerActor& player) override;
            void OnStep(::PlayerActor&, float) override {}

        private:
            NS::Coroutine Run(::PlayerActor& player);
        };
    } // namespace Player
} // namespace GL
