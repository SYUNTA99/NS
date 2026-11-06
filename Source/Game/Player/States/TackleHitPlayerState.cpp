#include "Game/Player/States/TackleHitPlayerState.h"

#include "Game/PlayerActor.h"

namespace GL
{
    namespace Player
    {
        void TackleHitPlayerState::OnEnter(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            (void)player;
        }
    } // namespace Player
} // namespace GL
