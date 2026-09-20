#include "Game/Player/States/LedgeHangingPlayerState.h"

#include "Game/Player/PlayerComponent.h"

namespace NS::Game::Player
{
    void LedgeHangingPlayerState::OnStep(PlayerComponent& player, float dt)
    {
        if (!player.HoldLedge())
        {
            return;
        }
        if (player.LedgeJump())
        {
            return;
        }
        if (player.ShouldClimbLedge())
        {
            player.ClimbLedge();
            return;
        }
        if (player.ShouldDropLedge())
        {
            player.DropLedge();
            return;
        }
        player.Shimmy(dt);
    }
} // namespace NS::Game::Player
