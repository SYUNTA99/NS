#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL::Player
{
        //! エアダッシュ。重力を止めて一定の速さでまっすぐ進み、動作 "AirDash" の長さで終わる。面に当たると地形の反動へ移る
        class AirDashPlayerState final : public PlayerState<AirDashPlayerState>
        {
        public:
            void OnEnter(::PlayerActor* player) override;
            void OnStep(::PlayerActor* player, float dt) override;
        };
} // namespace GL::Player
