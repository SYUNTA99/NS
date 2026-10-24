#include "Game/Player/States/FallPlayerState.h"

#include "Game/Player/States/IdlePlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"

namespace GL
{
    namespace Player
    {
        void FallPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->TickTimers(dt);
            player->AccelerateToInputDirection(dt);
            player->Jump(dt);
            player->CutJumpRelease();
            player->Gravity(dt);
            if (player->LedgeGrab())
            {
                return;
            }

            if (player->Body().IsGrounded())
            {
                player->States().Change<IdlePlayerState>();
            }
        }
    } // namespace Player
} // namespace GL
