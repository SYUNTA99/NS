#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL
{
    namespace Player
    {
        //! 外れの反動で着地した後の滑り。止まるまで操作を受けない
        class SkidPlayerState final : public PlayerState<SkidPlayerState>
        {
        public:
            void OnEnter(::PlayerActor& player) override;
            void OnStep(::PlayerActor& player, float dt) override;
        };
    } // namespace Player
} // namespace GL
