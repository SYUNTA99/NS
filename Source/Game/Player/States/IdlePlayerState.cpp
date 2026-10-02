#include "Game/Player/States/IdlePlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/WalkPlayerState.h"

namespace NS::Game::Player
{
    void IdlePlayerState::OnStep(::Player& player, float dt)
    {
        player.TickTimers(dt);
        player.ApplyFriction(dt);
        player.Jump(dt);
        player.CutJumpRelease();
        player.Gravity(dt);

        if (player.Movement().ShouldFall())
        {
            player.States().Change<FallPlayerState>(player);
        }
        else if (player.Movement().ShouldWalk())
        {
            player.States().Change<WalkPlayerState>(player);
        }
    }
} // namespace NS::Game::Player
