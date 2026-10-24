#include "Game/Player/States/BodySlamPlayerState.h"

#include "Game/PlayerActor.h"

namespace GL
{
    namespace Player
    {
        void BodySlamPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->UpdateBodySlam(dt);
        }
    } // namespace Player
} // namespace GL
