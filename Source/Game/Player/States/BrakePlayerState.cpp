#include "Game/Player/States/BrakePlayerState.h"

#include "Game/Player/PlayerJudges.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"

namespace GL::Player
{
        void BrakePlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->TickTimers(dt);
            player->ApplyBrake(dt);
            player->Jump(dt);
            player->CutJumpRelease();
            player->Gravity(dt);

            const NS::Body& body = player->Body();
            if (PlayerJudgeFall::Judge(body.IsGrounded()))
            {
                player->States().Change<FallPlayerState>();
            }
            else if (PlayerJudgeStopped::Judge(body.LateralVelocity()))
            {
                player->States().Change<IdlePlayerState>();
            }
        }
} // namespace GL::Player
