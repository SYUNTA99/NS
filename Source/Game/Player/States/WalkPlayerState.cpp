#include "Game/Player/States/WalkPlayerState.h"

#include "Game/Entity/EntityStateManagerComponent.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"

namespace NS::Game::Player
{
    void WalkPlayerState::OnStep(PlayerComponent& player, float dt)
    {
        player.TickTimers(dt);
        const bool brake = player.ShouldBrake();
        if (!brake && player.HasMoveInput())
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

        if (player.ShouldFall())
        {
            player.States()->Change<FallPlayerState>();
        }
        else if (brake)
        {
            player.States()->Change<BrakePlayerState>();
        }
        else if (player.ShouldIdle())
        {
            player.States()->Change<IdlePlayerState>();
        }
    }
} // namespace NS::Game::Player
