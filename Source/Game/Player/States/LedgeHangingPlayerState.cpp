#include "Game/Player/States/LedgeHangingPlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"

namespace NS::Game::Player
{
    void LedgeHangingPlayerState::OnStep(::Player& player, float dt)
    {
        if (!player.HoldLedge())
        {
            return;
        }
        if (player.LedgeJump())
        {
            return;
        }
        if (player.Movement().ShouldClimbLedge())
        {
            player.ClimbLedge();
            return;
        }
        if (player.Movement().ShouldDropLedge())
        {
            player.DropLedge();
            return;
        }
        player.Shimmy(dt);
    }
} // namespace NS::Game::Player
