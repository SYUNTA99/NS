#include "Game/Player/States/LedgeHangingPlayerState.h"

#include "Game/Player/PlayerJudges.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"

namespace GL::Player
{
        void LedgeHangingPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            if (!player->HoldLedge())
            {
                return;
            }
            if (player->LedgeJump())
            {
                return;
            }
            if (PlayerJudgeClimbLedge::Judge(player->ClimbForward()))
            {
                player->ClimbLedge();
                return;
            }
            if (PlayerJudgeDropLedge::Judge(player->Input().ReleaseLedgePressed()))
            {
                player->DropLedge();
                return;
            }
            player->Shimmy(dt);
        }
} // namespace GL::Player
