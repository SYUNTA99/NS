#include "Game/Player/States/BodySlamPlayerState.h"

#include "Game/Player/PlayerComponent.h"

namespace NS::Game::Player
{
    void BodySlamPlayerState::OnStep(PlayerComponent& player, float dt)
    {
        player.UpdateBodySlam(dt);
    }
} // namespace NS::Game::Player
