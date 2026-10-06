#include "Game/Player/States/SkidPlayerState.h"

#include "Game/Player.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "NSlib/Object/Components/Body.h"

namespace NS::Game::Player
{
    void SkidPlayerState::OnEnter(::Player& player)
    {
        player.BeginSkid();
    }

    void SkidPlayerState::OnStep(::Player& player, float dt)
    {
        player.TickTimers(dt);
        const bool stopped = player.AdvanceSkid();
        player.Gravity(dt);

        if (PlayerJudgeFall::Judge(player.Body().IsGrounded()))
        {
            player.States().Change<FallPlayerState>();
        }
        else if (stopped)
        {
            player.States().Change<IdlePlayerState>();
        }
    }
} // namespace NS::Game::Player
