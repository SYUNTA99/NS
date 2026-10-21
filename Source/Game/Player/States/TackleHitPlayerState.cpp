#include "Game/Player/States/TackleHitPlayerState.h"

#include "Game/PlayerActor.h"

namespace GL
{
    namespace Player
    {
        void TackleHitPlayerState::OnEnter(::PlayerActor& player)
        {
            player.BeginTackleHit();
        }
    } // namespace Player
} // namespace GL
