#include "Game/Player/States/AirDashPlayerState.h"

#include "Game/PlayerActor.h"

namespace GL
{
    namespace Player
    {
        void AirDashPlayerState::OnEnter(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->StartAirDashAction();
        }

        void AirDashPlayerState::OnStep(::PlayerActor* player, float)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            if (player->IsAirDashOver())
            {
                // 当たらずに終わった時だけ勢いを残す。当たった時は反動の速度が要るので OnExit には置かない
                player->KeepAirDashExitSpeed();
                player->ChangeToWalkOrFall();
                return;
            }
            player->KeepAirDashSpeed();
        }
    } // namespace Player
} // namespace GL
