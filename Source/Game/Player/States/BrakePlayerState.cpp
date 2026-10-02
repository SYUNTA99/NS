#include "Game/Player/States/BrakePlayerState.h"

#include "Runtime/Object/Components/Body.h"
#include "Game/Player.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"

namespace NS::Game::Player
{
    void BrakePlayerState::OnStep(::Player& player, float dt)
    {
        player.TickTimers(dt);
        player.ApplyBrake(dt);
        player.Jump(dt);
        player.CutJumpRelease();
        player.Gravity(dt);

        const NS::Obj::Body& body = player.Body();
        if (PlayerJudgeFall::Judge(body.IsGrounded()))
        {
            player.States().Change<FallPlayerState>(player);
        }
        else if (PlayerJudgeStopped::Judge(body.LateralVelocity()))
        {
            player.States().Change<IdlePlayerState>(player);
        }
    }
} // namespace NS::Game::Player
