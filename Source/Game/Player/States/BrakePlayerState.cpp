#include "Game/Player/States/BrakePlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
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
            player.States().Change<FallPlayerState>(player);
        }
        else if (player.Movement().IsStopped())
        {
            player.States().Change<IdlePlayerState>(player);
        }
    }
} // namespace NS::Game::Player
