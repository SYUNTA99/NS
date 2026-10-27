#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL::Player
{
        //! @brief 地形の面で返った反動。操作なしの段 → 曲げとダッシュ 1 回の補正の段 → 落下の状態、の順に秒で移る
        //! @details 段の秒は自機の値 (AirDashStatus) が数える。状態は値を持たない
        class DashReboundPlayerState final : public PlayerState<DashReboundPlayerState>
        {
        public:
            void OnStep(::PlayerActor* player, float dt) override;
        };
} // namespace GL::Player
