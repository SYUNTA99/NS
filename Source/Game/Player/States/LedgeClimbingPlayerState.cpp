#include "Game/Player/States/LedgeClimbingPlayerState.h"

#include "Game/PlayerActor.h"

namespace GL::Player
{
        void LedgeClimbingPlayerState::OnEnter(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            StartCoroutine(Run(player));
        }

        NS::Coroutine LedgeClimbingPlayerState::Run(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            while (true)
            {
                co_await NS::NextFrame{};
                player->UpdateLedgeClimb(StepDelta());
                if (!player->States().IsCurrent<LedgeClimbingPlayerState>())
                {
                    co_return;
                }
            }
        }
} // namespace GL::Player
