#include "Game/Player/States/IdlePlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/PlayerStateManager.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"

namespace NS::Game::Player
{
    void IdlePlayerState::OnStep(::Player& player, float dt)
    {
        player.Movement().TickTimers(dt);
        player.Movement().ApplyFriction(dt);
        player.Movement().Jump(dt);
        player.Movement().CutJumpRelease();
        player.Movement().Gravity(dt);

        if (player.Movement().ShouldFall())
        {
            player.StateManager().Change<FallPlayerState>();
        }
        else if (player.Movement().ShouldWalk())
        {
            player.StateManager().Change<WalkPlayerState>();
        }
    }
} // namespace NS::Game::Player
