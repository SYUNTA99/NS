#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL
{
    namespace Player
    {
        //! @brief 体当たりが当たってから、止めが明けて反動へ移るまで
        //! @details 検知の次のフレームに入り、段の束を始める。止めの間は基底が状態の段を飛ばす。
        //! 明けは状態の段より前の決定の段で PlayerActor が出し、反動の 1 歩目を明けのフレームに回す
        class TackleHitPlayerState final : public PlayerState<TackleHitPlayerState>
        {
        public:
            void OnEnter(::PlayerActor* player) override;
            void OnStep(::PlayerActor* player, float) override
            {
                NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
                (void)player;
            }
        };
    } // namespace Player
} // namespace GL
