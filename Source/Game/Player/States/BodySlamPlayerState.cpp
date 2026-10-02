#include "Game/Player/States/BodySlamPlayerState.h"

#include "Game/Player.h"

namespace NS::Game::Player
{
    void BodySlamPlayerState::OnStep(::Player& player, float dt)
    {
        player.UpdateBodySlam(dt);
    }
} // namespace NS::Game::Player
