#include "Game/Player/States/SkidPlayerState.h"

#include "Game/Player/PlayerJudges.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/SubObjects/Body.h"

namespace GL
{
    namespace Player
    {
        void SkidPlayerState::OnEnter(::PlayerActor& player)
        {
            player.BeginSkid();
        }

        void SkidPlayerState::OnStep(::PlayerActor& player, float dt)
        {
            player.TickTimers(dt);
            const bool stopped = player.AdvanceSkid();
            player.Gravity(dt);

            if (PlayerJudgeFall::Judge(player.Body().IsGrounded()))
            {
                player.States().Change<FallPlayerState>();
            }
            else if (stopped)
            {
                player.States().Change<IdlePlayerState>();
            }
        }
    } // namespace Player
} // namespace GL
