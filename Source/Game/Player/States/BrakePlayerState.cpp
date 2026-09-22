#include "Game/Player/States/BrakePlayerState.h"

#include "Game/Entity/EntityStateManager.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"

namespace NS::Game::Player
{
    void BrakePlayerState::OnStep(PlayerComponent& player, float dt)
    {
        player.TickTimers(dt);
        player.ApplyBrake(dt);
        player.Jump(dt);
        player.CutJumpRelease();
        player.Gravity(dt);

        if (player.ShouldFall())
        {
            player.States()->Change<FallPlayerState>();
        }
        else if (player.IsStopped())
        {
            player.States()->Change<IdlePlayerState>();
        }
    }
} // namespace NS::Game::Player
