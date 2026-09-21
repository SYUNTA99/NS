#include "Game/Player/States/FallPlayerState.h"

#include "Game/Entity/EntityStateManagerComponent.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/States/IdlePlayerState.h"

namespace NS::Game::Player
{
    void FallPlayerState::OnStep(PlayerComponent& player, float dt)
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

        if (player.IsGrounded())
        {
            player.States()->Change<IdlePlayerState>();
        }
    }
} // namespace NS::Game::Player
