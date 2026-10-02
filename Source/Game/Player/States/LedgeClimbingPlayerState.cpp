#include "Game/Player/States/LedgeClimbingPlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"

namespace NS::Game::Player
{
    void LedgeClimbingPlayerState::OnEnter(::Player& player)
    {
        StartCoroutine(Run(player));
    }

    NS::Core::Coroutine LedgeClimbingPlayerState::Run(::Player& player)
    {
        while (true)
        {
            co_await NS::Core::NextFrame{};
            player.Movement().UpdateLedgeClimb(StepDelta());
            if (!player.States().IsCurrent<LedgeClimbingPlayerState>())
            {
                co_return;
            }
        }
    }
} // namespace NS::Game::Player
