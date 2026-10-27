#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL
{
    namespace Player
    {
        //! 突進の線の先の相手に触れる前の、当たる直前の突進
        //! 突進を続けながら、入る時に段の当たる直前の動作を始める。検知で突進を打ち切って出る時は
        //! 形と自機以外の止めを触れる瞬間まで残し、予測が外れて出る時は戻す
        class BeforeContactPlayerState final : public PlayerState<BeforeContactPlayerState>
        {
        public:
            void OnEnter(::PlayerActor* player) override;
            void OnStep(::PlayerActor* player, float dt) override;
            void OnExit(::PlayerActor* player) override;
        };
    } // namespace Player
} // namespace GL
