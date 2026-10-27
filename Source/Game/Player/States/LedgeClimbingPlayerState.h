#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL::Player
{
        //! よじ登り。登り切ったフレームに立ちへ移る
        class LedgeClimbingPlayerState final : public PlayerState<LedgeClimbingPlayerState>
        {
        public:
            void OnEnter(::PlayerActor* player) override;
            void OnStep(::PlayerActor* player, float) override
            {
                NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
                (void)player;
            }

        private:
            NS::Coroutine Run(::PlayerActor* player);
        };
} // namespace GL::Player
