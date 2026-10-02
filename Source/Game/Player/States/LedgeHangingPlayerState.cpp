#include "Game/Player/States/LedgeHangingPlayerState.h"

#include "Runtime/Object/Components/Body.h"
#include "Game/Player.h"
#include "Game/Player/PlayerJudges.h"

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
        if (PlayerJudgeClimbLedge::Judge(player.ClimbForward()))
        {
            player.ClimbLedge();
            return;
        }
        if (PlayerJudgeDropLedge::Judge(player.Input().ReleaseLedgePressed()))
        {
            player.DropLedge();
            return;
        }
        player.Shimmy(dt);
    }
} // namespace NS::Game::Player
