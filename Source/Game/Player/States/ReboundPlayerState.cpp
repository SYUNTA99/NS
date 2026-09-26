#include "Game/Player/States/ReboundPlayerState.h"

#include "Game/Entity/EntityStateManager.h"
#include "Game/Player/PlayerComponent.h"
#include "Game/Player/States/IdlePlayerState.h"

namespace NS::Game::Player
{
    void ReboundPlayerState::OnStep(PlayerComponent& player, float dt)
    {
        // Jump と CutJumpRelease は呼ばない。跳ぶと反動の縦速度が書き換わる。上りで離すと切られる
        // どちらも同じ当て方で違う軌道になる
        player.TickTimers(dt);
        player.AccelerateDuringRebound(dt);
        player.ReboundGravity(dt);
        if (player.LedgeGrab())
        {
            return;
        }

        if (player.ShouldLand())
        {
            player.States()->Change<IdlePlayerState>();
        }
    }
} // namespace NS::Game::Player
