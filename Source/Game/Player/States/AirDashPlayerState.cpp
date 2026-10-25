#include "Game/Player/States/AirDashPlayerState.h"

#include "Game/PlayerActor.h"

namespace GL
{
    namespace Player
    {
        void AirDashPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->UpdateAirDash(dt);
        }
    } // namespace Player
} // namespace GL
