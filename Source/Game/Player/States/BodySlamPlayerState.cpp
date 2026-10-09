#include "Game/Player/States/BodySlamPlayerState.h"

#include "Game/Player.h"

namespace GL::Player
{
    void BodySlamPlayerState::OnStep(::Player& player, float dt)
    {
        player.UpdateBodySlam(dt);
    }
} // namespace GL::Player
