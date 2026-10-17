#include "Game/Player/States/LedgeHangingPlayerState.h"

#include "Game/Player/PlayerJudges.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"

namespace GL
{
    namespace Player
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
    } // namespace Player
} // namespace GL
