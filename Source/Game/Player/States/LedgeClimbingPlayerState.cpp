#include "Game/Player/States/LedgeClimbingPlayerState.h"

#include "Game/Player.h"

namespace NS::Game::Player
{
    void LedgeClimbingPlayerState::OnEnter(::Player& player)
    {
        StartCoroutine(Run(player));
    }

    NS::Coroutine LedgeClimbingPlayerState::Run(::Player& player)
    {
        while (true)
        {
            co_await NS::NextFrame{};
            player.UpdateLedgeClimb(StepDelta());
            if (!player.States().IsCurrent<LedgeClimbingPlayerState>())
            {
                co_return;
            }
        }
    }
} // namespace NS::Game::Player
