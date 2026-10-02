#include "Game/Player/States/WalkPlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/States/BrakePlayerState.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"

namespace NS::Game::Player
{
    void WalkPlayerState::OnStep(::Player& player, float dt)
    {
        player.Movement().TickTimers(dt);
        const bool brake = player.Movement().ShouldBrake();
        if (!brake && player.Movement().HasMoveInput())
        {
            player.Movement().AccelerateToInputDirection(dt);
        }
        else if (!brake)
        {
            player.Movement().ApplyFriction(dt);
        }
        player.Movement().Jump(dt);
        player.Movement().CutJumpRelease();
        player.Movement().Gravity(dt);

        if (player.Movement().ShouldFall())
        {
            player.States().Change<FallPlayerState>(player);
        }
        else if (brake)
        {
            player.States().Change<BrakePlayerState>(player);
        }
        else if (player.Movement().ShouldIdle())
        {
            player.States().Change<IdlePlayerState>(player);
        }
    }
} // namespace NS::Game::Player
