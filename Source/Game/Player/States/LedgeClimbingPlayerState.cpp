#include "Game/Player/States/LedgeClimbingPlayerState.h"

#include "Game/Player/PlayerComponent.h"

namespace NS::Game::Player
{
    void LedgeClimbingPlayerState::OnStep(PlayerComponent& player, float dt)
    {
        player.UpdateLedgeClimb(dt);
    }
} // namespace NS::Game::Player
