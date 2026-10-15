#include "Game/Player/States/BodySlamPlayerState.h"

#include "Game/PlayerActor.h"

namespace GL::Player
{
    void BodySlamPlayerState::OnStep(::PlayerActor& player, float dt)
    {
        player.UpdateBodySlam(dt);
    }
} // namespace GL::Player
