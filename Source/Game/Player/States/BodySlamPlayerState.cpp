#include "Game/Player/States/BodySlamPlayerState.h"

#include "Game/Player/PlayerComponent.h"

namespace NS::Game::Player
{
    void BodySlamPlayerState::OnStep(PlayerComponent& player, float dt)
    {
        player.UpdateBodySlam(dt);
    }

    NS_STATE(BodySlamPlayerState, NS::Game::Player::PlayerComponent)
} // namespace NS::Game::Player
