#include "Game/Player/States/FallPlayerState.h"

#include "Game/PlayerActor.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "NSlib/Object/SubObjects/Body.h"

namespace GL::Player
{
    void FallPlayerState::OnStep(::PlayerActor& player, float dt)
    {
        player.TickTimers(dt);
        player.AccelerateToInputDirection(dt);
        player.Jump(dt);
        player.CutJumpRelease();
        player.Gravity(dt);
        if (player.LedgeGrab())
        {
            return;
        }

        if (player.Body().IsGrounded())
        {
            player.States().Change<IdlePlayerState>();
        }
    }
} // namespace GL::Player
