#include "Game/Player/States/IdlePlayerState.h"

#include "Game/Entity/EntityStateManager.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"

namespace NS::Game::Player
{
    void IdlePlayerState::OnStep(PlayerComponent& player, float dt)
    {
        player.TickTimers(dt);
        player.ApplyFriction(dt);
        player.Jump(dt);
        player.CutJumpRelease();
        player.Gravity(dt);

        if (player.ShouldFall())
        {
            player.States()->Change<FallPlayerState>();
        }
        else if (player.ShouldWalk())
        {
            player.States()->Change<WalkPlayerState>();
        }
    }
} // namespace NS::Game::Player
