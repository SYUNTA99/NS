#include "Game/Player/States/ReboundPlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Runtime/Object/Components/Body.h"

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
            if (player.LedgeGrab())
            {
                co_return;
            }

            if (PlayerJudgeLand::Judge(player.Body().IsGrounded(), player.Body().VerticalVelocity()))
            {
                player.States().Change<IdlePlayerState>(player);
                co_return;
            }
        }
    }
} // namespace NS::Game::Player
