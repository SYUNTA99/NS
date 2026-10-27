#include "Game/Player/States/IdlePlayerState.h"

#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"

namespace GL
{
    namespace Player
    {
        void IdlePlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->TickTimers(dt);
            player->ApplyFriction(dt);
            player->Jump(dt);
            player->CutJumpRelease();
            player->Gravity(dt);

            const NS::Body& body = player->Body();
            const bool hasInput =
                PlayerJudgeMoveInput::Judge(player->DesiredSpeedScale(), player->Params().StickDeadzone());
            const bool stopped = PlayerJudgeStopped::Judge(body.LateralVelocity());
            if (PlayerJudgeFall::Judge(body.IsGrounded()))
            {
                player->States().Change<FallPlayerState>();
            }
            else if (PlayerJudgeWalk::Judge(body.IsGrounded(), hasInput, stopped))
            {
                player->States().Change<WalkPlayerState>();
            }
        }
    } // namespace Player
} // namespace GL
