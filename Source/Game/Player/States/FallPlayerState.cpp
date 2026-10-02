#include "Game/Player/States/FallPlayerState.h"

#include "Game/Entity/EntityComponent.h"
#include "Game/Player.h"
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
        if (player.LedgeGrab())
        {
            return;
        }

        if (player.Body().IsGrounded())
        {
            player.States().Change<IdlePlayerState>(player);
        }
    }
} // namespace NS::Game::Player
