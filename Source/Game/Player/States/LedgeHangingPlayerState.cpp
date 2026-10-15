#include "Game/Player/States/LedgeHangingPlayerState.h"

#include "Game/PlayerActor.h"
#include "Game/Player/PlayerJudges.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/SubObjects/PlayerInput.h"

namespace GL::Player
{
    void LedgeHangingPlayerState::OnStep(::PlayerActor& player, float dt)
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
} // namespace GL::Player
