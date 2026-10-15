#include "Game/Player/States/BodySlamPlayerState.h"

#include "Game/PlayerActor.h"

namespace GL
{
    namespace Player
    {
        void BodySlamPlayerState::OnStep(::PlayerActor& player, float dt)
        {
            player.UpdateBodySlam(dt);
        }
    } // namespace Player
} // namespace GL
