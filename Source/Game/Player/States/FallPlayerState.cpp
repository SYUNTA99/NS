#include "Game/Player/States/FallPlayerState.h"

#include "Game/Player.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Runtime/Object/Components/Body.h"

namespace NS::Game::Player
{
    void FallPlayerState::OnStep(::Player& player, float dt)
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
} // namespace NS::Game::Player
