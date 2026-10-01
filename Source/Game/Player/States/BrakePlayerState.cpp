#include "Game/Player/States/BrakePlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerStateManager.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"

namespace NS::Game::Player
{
    void BrakePlayerState::OnStep(::Player& player, float dt)
    {
        player.Movement().TickTimers(dt);
        player.Movement().ApplyBrake(dt);
        player.Movement().Jump(dt);
        player.Movement().CutJumpRelease();
        player.Movement().Gravity(dt);

        if (player.Movement().ShouldFall())
        {
            player.StateManager().Change<FallPlayerState>();
        }
        else if (player.Movement().IsStopped())
        {
            player.StateManager().Change<IdlePlayerState>();
        }
    }
} // namespace NS::Game::Player
