#include "Game/Player/States/WalkPlayerState.h"

#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"

namespace GL
{
    namespace Player
    {
        void WalkPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->TickTimers(dt);
            const NS::Body& body = player->Body();
            const PlayerParams& params = player->Params();
            const bool hasInput = PlayerJudgeMoveInput::Judge(player->DesiredSpeedScale(), params.StickDeadzone());
            const bool brake = PlayerJudgeBrake::Judge(
                hasInput, player->DesiredDirection(), body.LateralVelocity(), params.BrakeThreshold());
            if (!brake && hasInput)
            {
                player->AccelerateToInputDirection(dt);
            }
            else if (!brake)
            {
                player->ApplyFriction(dt);
            }
            player->Jump(dt);
            player->CutJumpRelease();
            player->Gravity(dt);

            const bool stopped = PlayerJudgeStopped::Judge(body.LateralVelocity());
            if (PlayerJudgeFall::Judge(body.IsGrounded()))
            {
                player->States().Change<FallPlayerState>();
            }
            else if (brake)
            {
                player->States().Change<BrakePlayerState>();
            }
            else if (PlayerJudgeIdle::Judge(body.IsGrounded(), hasInput, stopped))
            {
                player->States().Change<IdlePlayerState>();
            }
        }
    } // namespace Player
} // namespace GL
