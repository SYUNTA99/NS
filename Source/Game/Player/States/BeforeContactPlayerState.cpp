#include "Game/Player/States/BeforeContactPlayerState.h"

#include "Game/PlayerActor.h"

namespace GL::Player
{
        void BeforeContactPlayerState::OnEnter(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->BeginBeforeContact();
        }

        void BeforeContactPlayerState::OnStep(::PlayerActor* player, float dt)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->UpdateBodySlam(dt);
        }

        void BeforeContactPlayerState::OnExit(::PlayerActor* player)
        {
            NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
            player->EndBeforeContact();
        }
} // namespace GL::Player
