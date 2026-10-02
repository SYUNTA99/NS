#include "Game/Player/States/FallPlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/States/IdlePlayerState.h"

namespace NS::Game::Player
{
    void FallPlayerState::OnStep(::Player& player, float dt)
    {
        player.TickTimers(dt);
        player.AccelerateToInputDirection(dt);
        player.Jump(dt);
        player.CutJumpRelease();
        player.Gravity(dt);
        if (player.Movement().LedgeGrab())
        {
            return;
        }

        if (player.Movement().IsGrounded())
        {
            player.States().Change<IdlePlayerState>(player);
        }
    }
} // namespace NS::Game::Player
