#include "Game/Player/States/LedgeClimbingPlayerState.h"

#include "Game/PlayerActor.h"

namespace GL
{
    namespace Player
    {
        void LedgeClimbingPlayerState::OnEnter(::PlayerActor& player)
        {
            StartCoroutine(Run(player));
        }

        NS::Coroutine LedgeClimbingPlayerState::Run(::PlayerActor& player)
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
    } // namespace Player
} // namespace GL
