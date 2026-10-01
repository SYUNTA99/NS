#include "Game/Player/States/BodySlamPlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"

namespace NS::Game::Player
{
    void BodySlamPlayerState::OnStep(::Player& player, float dt)
    {
        player.Movement().UpdateBodySlam(dt);
    }
} // namespace NS::Game::Player
