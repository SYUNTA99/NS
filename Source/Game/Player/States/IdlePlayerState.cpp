#include "Game/Player/States/IdlePlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"

namespace NS::Game::Player
{
    void IdlePlayerState::OnStep(::Player& player, float dt)
    {
        player.TickTimers(dt);
        player.ApplyFriction(dt);
        player.Jump(dt);
        player.CutJumpRelease();
        player.Gravity(dt);

        const PlayerComponent& body = player.Movement();
        if (PlayerJudgeFall::Judge(body.IsGrounded()))
        {
            player.States().Change<FallPlayerState>(player);
        }
        else if (PlayerJudgeWalk::Judge(
                     body.IsGrounded(),
                     PlayerJudgeMoveInput::Judge(body.DesiredSpeedScale(), player.Params().m_stickDeadzone),
                     PlayerJudgeStopped::Judge(body.LateralVelocity())))
        {
            player.States().Change<WalkPlayerState>(player);
        }
    }
} // namespace NS::Game::Player
