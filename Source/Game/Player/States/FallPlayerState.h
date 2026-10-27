#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL
{
    namespace Player
    {
        /// 落下。着地したフレームに立ちへ移る
        class FallPlayerState final : public PlayerState<FallPlayerState>
        {
        public:
            void OnStep(::PlayerActor* player, float dt) override;
        };
    } // namespace Player
} // namespace GL
