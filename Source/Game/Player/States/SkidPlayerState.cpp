#include "Game/Player/States/SkidPlayerState.h"

#include "Game/Player/PlayerJudges.h"
#include "Game/Player/States/FallPlayerState.h"
#include "Game/Player/States/IdlePlayerState.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"

namespace GL::Player
{
        void SkidPlayerState::OnEnter(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->BeginSkid();
        }

        void SkidPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->TickTimers(dt);
            const bool stopped = player->AdvanceSkid();
            player->Gravity(dt);

            if (PlayerJudgeFall::Judge(player->Body().IsGrounded()))
            {
                player->States().Change<FallPlayerState>();
            }
            else if (stopped)
            {
                player->States().Change<IdlePlayerState>();
            }
        }
} // namespace GL::Player
