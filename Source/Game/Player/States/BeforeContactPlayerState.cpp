#include "Game/Player/States/BeforeContactPlayerState.h"

#include "Game/PlayerActor.h"

namespace GL
{
    namespace Player
    {
        void BeforeContactPlayerState::OnEnter(::PlayerActor& player)
        {
            player.BeginBeforeContact();
        }

        void BeforeContactPlayerState::OnStep(::PlayerActor& player, float dt)
        {
            player.UpdateBodySlam(dt);
        }

        void BeforeContactPlayerState::OnExit(::PlayerActor& player)
        {
            player.EndBeforeContact();
        }
    } // namespace Player
} // namespace GL
