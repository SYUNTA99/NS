#include "Game/Player/States/ReboundPlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/States/IdlePlayerState.h"

namespace NS::Game::Player
{
    void ReboundPlayerState::OnEnter(::Player& player)
    {
        StartCoroutine(Run(player));
    }

    NS::Core::Coroutine ReboundPlayerState::Run(::Player& player)
    {
        while (true)
        {
            co_await NS::Core::NextFrame{};
            const float dt = StepDelta();
            player.TickTimers(dt);
            player.AccelerateDuringRebound(dt);
            player.ReboundGravity(dt);
            if (player.Movement().LedgeGrab())
            {
                co_return;
            }

            if (player.Movement().ShouldLand())
            {
                player.States().Change<IdlePlayerState>(player);
                co_return;
            }
        }
    }
} // namespace NS::Game::Player
