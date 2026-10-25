#include "Game/Player/States/DashReboundPlayerState.h"

#include "Game/PlayerActor.h"

namespace GL
{
    namespace Player
    {
        void DashReboundPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->UpdateDashRebound(dt);
        }
    } // namespace Player
} // namespace GL
