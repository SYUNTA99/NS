#include "Game/Player/States/AirDashPlayerState.h"

#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Action/ActionTimeline.h"

namespace GL::Player
{
        void AirDashPlayerState::OnEnter(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            // 動作が始まらなければ終わりが来ないので、入った所で普段の動きへ移る。予約は次の確定で効く
            if (!player->StartAirDashAction())
            {
                player->ChangeToWalkOrFall();
            }
        }

        void AirDashPlayerState::OnStep(::PlayerActor* player, float)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            if (NS::IsStateEnd(player))
            {
                // 当たらずに終わった時だけ勢いを残す。当たった時は反動の速度が要るので OnExit には置かない
                player->KeepAirDashExitSpeed();
                player->ChangeToWalkOrFall();
                return;
            }
            player->KeepAirDashSpeed();
        }
} // namespace GL::Player
