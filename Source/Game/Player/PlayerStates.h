#pragma once

#include "Game/Player/PlayerState.h"

class PlayerActor;

namespace GL
{
    namespace Player
    {
        class IdlePlayerState final : public PlayerState<IdlePlayerState>
        {
        public:
            void OnStep(::PlayerActor* player, float dt) override;
        };

        class WalkPlayerState final : public PlayerState<WalkPlayerState>
        {
        public:
            void OnStep(::PlayerActor* player, float dt) override;
        };

        //! 止まるまで入力方向へ加速しないブレーキ
        class BrakePlayerState final : public PlayerState<BrakePlayerState>
        {
        public:
            void OnStep(::PlayerActor* player, float dt) override;
        };

        //! 落下。着地したフレームに立ちへ移る
        class FallPlayerState final : public PlayerState<FallPlayerState>
        {
        public:
            void OnStep(::PlayerActor* player, float dt) override;
        };

        class LedgeHangingPlayerState final : public PlayerState<LedgeHangingPlayerState>
        {
        public:
            void OnStep(::PlayerActor* player, float dt) override;
        };

        //! よじ登り。登り切ったフレームに立ちへ移る
        class LedgeClimbingPlayerState final : public PlayerState<LedgeClimbingPlayerState>
        {
        public:
            void OnEnter(::PlayerActor* player) override;
            void OnStep(::PlayerActor* player, float) override
            {
                NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
                (void)player;
            }

        private:
            NS::Coroutine Run(::PlayerActor* player);
        };

        //! エアダッシュ。重力を止めて一定の速さでまっすぐ進み、動作 "AirDash" の長さで終わる。面に当たると地形の反動へ移る
        class AirDashPlayerState final : public PlayerState<AirDashPlayerState>
        {
        public:
            void OnEnter(::PlayerActor* player) override;
            void OnStep(::PlayerActor* player, float dt) override;
        };

        class BodySlamPlayerState final : public PlayerState<BodySlamPlayerState>
        {
        public:
            void OnStep(::PlayerActor* player, float dt) override;
        };

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

        //! 体当たりが当たってから、止めが明けて反動へ移るまで
        //! 検知の次のフレームに入り、段の束を始める。止めの間は基底が状態の段を飛ばす。
        //! 明けは状態の段より前の決定の段で PlayerActor が出し、反動の 1 歩目を明けのフレームに回す
        class TackleHitPlayerState final : public PlayerState<TackleHitPlayerState>
        {
        public:
            void OnEnter(::PlayerActor* player) override;
            void OnStep(::PlayerActor* player, float) override
            {
                NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
                (void)player;
            }
        };

        class ReboundPlayerState final : public PlayerState<ReboundPlayerState>
        {
        public:
            void OnEnter(::PlayerActor* player) override;
            void OnStep(::PlayerActor* player, float) override
            {
                NS_ASSERT(Game, player != nullptr, "状態の持ち主が無い");
                (void)player;
            }

        private:
            NS::Coroutine Run(::PlayerActor* player);
        };

        //! 地形の面で返った反動。操作なしの段 → 曲げとダッシュ 1 回の補正の段 → 落下の状態、の順に秒で移る
        //! 段の秒は自機の値 (AirDashStatus) が数える。状態は値を持たない
        class DashReboundPlayerState final : public PlayerState<DashReboundPlayerState>
        {
        public:
            void OnStep(::PlayerActor* player, float dt) override;
        };

        //! 外れの反動で着地した後の滑り。止まるまで操作を受けない
        class SkidPlayerState final : public PlayerState<SkidPlayerState>
        {
        public:
            void OnEnter(::PlayerActor* player) override;
            void OnStep(::PlayerActor* player, float dt) override;
        };
    } // namespace Player
} // namespace GL
