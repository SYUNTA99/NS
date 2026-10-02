#include "Game/Player/States/WalkPlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"

namespace NS::Game::Player
{
    void WalkPlayerState::OnStep(::Player& player, float dt)
    {
        player.TickTimers(dt);
        const PlayerComponent& body = player.Movement();
        const PlayerParams& params = player.Params();
        const bool hasInput = PlayerJudgeMoveInput::Judge(body.DesiredSpeedScale(), params.m_stickDeadzone);
        const bool brake =
            PlayerJudgeBrake::Judge(hasInput, body.DesiredDirection(), body.LateralVelocity(), params.m_brakeThreshold);
        if (!brake && hasInput)
        {
            player.AccelerateToInputDirection(dt);
        }
        else if (!brake)
        {
            player.ApplyFriction(dt);
        }
        player.Jump(dt);
        player.CutJumpRelease();
        player.Gravity(dt);

        if (PlayerJudgeFall::Judge(body.IsGrounded()))
        {
            player.States().Change<FallPlayerState>(player);
        }
        else if (brake)
        {
            player.States().Change<BrakePlayerState>(player);
        }
        else if (PlayerJudgeIdle::Judge(body.IsGrounded(),
                                        PlayerJudgeWalk::Judge(body.IsGrounded(),
                                                               hasInput,
                                                               PlayerJudgeStopped::Judge(body.LateralVelocity()))))
        {
            player.States().Change<IdlePlayerState>(player);
        }
    }
} // namespace NS::Game::Player
