#include "Game/Player/States/LedgeHangingPlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"

namespace NS::Game::Player
{
    void LedgeHangingPlayerState::OnStep(::Player& player, float dt)
    {
        if (!player.Movement().HoldLedge())
        {
            return;
        }
        if (player.Movement().LedgeJump())
        {
            return;
        }
        if (player.Movement().ShouldClimbLedge())
        {
            player.Movement().ClimbLedge();
            return;
        }
        if (player.Movement().ShouldDropLedge())
        {
            player.Movement().DropLedge();
            return;
        }
        player.Movement().Shimmy(dt);
    }
} // namespace NS::Game::Player
