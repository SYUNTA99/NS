#pragma once

#include "Game/Entity/EntityComponent.h"
#include "Game/Player/PlayerEvents.h"
#include "Game/Player/PlayerStats.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Reflection/Reflection.h"

namespace NS::Game::Entity
{
    class EntityStateManagerComponent;
}

namespace NS::Game::Player
{
    class PlayerStateManagerComponent;

    //! @brief 自機の能力を持つ Component
    //! @details 移動と接地は EntityComponent が持ち、ここには自機だけの能力と条件判定を置く
    //! 調整値は自分の欄として持つ。Inspector とシーン JSON は getter と setter を通してこの欄を読み書きする
    //! 状態は能力呼びの列だけにするので、状態から呼ぶ動詞は public
    //! 依存: NS::Game::Entity::EntityComponent / EntityStateManagerComponent, PlayerStats
    class PlayerComponent : public NS::Game::Entity::EntityComponent
    {
    public:
        //! world 空間の目標移動方向と速度スケールを渡す。スケールは 0..1 に丸める
        void SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept;
        //! 直近に渡された目標速度スケール 0..1
        [[nodiscard]] float DesiredSpeedScale() const noexcept { return m_desiredSpeedScale; }
        //! 直近に渡された world 空間の目標移動方向。長さは入力の強さのままで正規化されていない
        [[nodiscard]] NS::Core::Vector3 DesiredDirection() const noexcept { return m_desiredDir; }

        //! 掴まり中の生ローカル入力で各成分は -1..1。SetDesiredMove とは別に持つ
        void SetClimbMove(float localRight, float localForward) noexcept;
        [[nodiscard]] float ClimbRight() const noexcept { return m_climbRight; }     //!< 掴まり中の左右入力
        [[nodiscard]] float ClimbForward() const noexcept { return m_climbForward; } //!< 掴まり中の前後入力

        //! ジャンプの押下を 1 回ぶん立てる。更新の終わりに落ちるので次のフレームには残らない
        void SetJumpPressed() noexcept;
        //! 手放しの押下を 1 回ぶん立てる。更新の終わりに落ちるので次のフレームには残らない
        void SetReleaseLedgePressed() noexcept;
        //! ジャンプボタンの長押し状態を渡す。上昇中に離すと縦速度を縮める
        void SetJumpHeld(bool held) noexcept;
        [[nodiscard]] int JumpsRemaining() const noexcept { return m_jumpsRemaining; } //!< 残りジャンプ回数

        [[nodiscard]] float MaxSpeed() const noexcept { return m_maxSpeed; } //!< 走行の最高速度
        //! 最高速度を外から差し替える。負は 0 へ丸め、非有限値は書き込まない
        void SetMaxSpeed(float speed) noexcept;

        //! 奈落落ちの復活などで速度・接地・ジャンプまわりの記録と状態機械を初期状態へ戻す
        void ResetState() noexcept;

        //! 状態が次の状態へ移る時に呼ぶ状態管理。HandleStates はこれが無いと状態を進めないので、状態の中では非 null
        [[nodiscard]] NS::Game::Entity::EntityStateManagerComponent* States() const noexcept;

        //! 体当たりの発動を要求する
        //! @details 溜め量 0 はタップの飛び込みで、非有限値は 0 とみなす。
        //! そのフレームで出せない要求は先行入力時間だけ覚え、過ぎたら失効する。
        //! 1 度出すと接地するまで次は出せない
        //! @param[in] charge01 溜め量 0..1
        void RequestBodySlam(float charge01) noexcept;
        //! 突進中の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsBodySlamming() const noexcept;
        //! 突進の進み具合 0..1。突進中でなければ 0
        [[nodiscard]] float BodySlamProgress01() const noexcept;
        [[nodiscard]] float BodySlamCharge01() const noexcept { return m_bodySlamCharge01; } //!< 発動時の溜め量 0..1
        //! 衝突の裁定が読む速度。突進中は向きと突進速度から作る
        //! @details 実速度は壁へ押し付けられたフレームで 0 に潰れ、衝突の先読みが今の位置から動かなくなる
        [[nodiscard]] NS::Core::Vector3 BodySlamVelocity() const noexcept;
        //! 突進を打ち切って通常移動へ戻す。突進中でなければ何もしない
        void CancelBodySlam() noexcept;

        //! 向きを解決して突進を始める。向きが決まらないか距離が 0 以下の場合 false、それ以外の場合は true
        //! @details 向きは 入力の水平 → カメラの水平前方 → 現在速度の水平 の順で解決する
        [[nodiscard]] bool BodySlam() noexcept;

        //! 突進の 1 フレームを進める。距離を使い切るか、発動したフレームより後に進めないと通常移動へ戻す
        void UpdateBodySlam(float dt) noexcept;

        // 移動の 1 フレームを作る動詞。呼ぶ順序がそのまま手触りになる
        //! 先行入力とコヨーテ猶予のタイマーを 1 フレーム進める
        void TickTimers(float dt) noexcept;
        //! @brief 入力の向きへ加速する。入力が無ければ何もしない
        //! @details 加速の上限は走行の最高速度 × 倒し具合で、歩き速度を下回らない。空中は空中の加速度を使う
        void AccelerateToInputDirection(float dt) noexcept;
        //! 手を放した時の減速度で水平の速さを減らす
        void ApplyFriction(float dt) noexcept;
        //! ブレーキの減速度で水平の速さを減らす
        void ApplyBrake(float dt) noexcept;
        //! 接地かコヨーテ猶予の内で押されていれば跳ぶ
        void Jump(float dt) noexcept;
        //! 上昇中にボタンを離したフレームだけ縦速度を縮める
        void CutJumpRelease() noexcept;
        //! 上昇と下降で非対称な重力を当てる。頂点の近くは弱める
        void Gravity(float dt) noexcept;
        //! 調整値の登れる段の高さを渡して 1 フレーム動かす
        void Move(float dt) noexcept;
        //! タップの飛び込みだけに当てる重力。滞空秒がタップ距離を進む秒と揃う強さにする
        void TapSlamGravity(float dt) noexcept;
        //! 着地でジャンプ回数を戻し、接地中はコヨーテ猶予と突進の使用済みを戻す
        void SyncGroundState() noexcept;
        //! 縁を掴めるか試す。掴んだ場合 true、それ以外の場合は false。true なら呼び出し側は即 return する
        [[nodiscard]] bool LedgeGrab() noexcept;

        //! @brief 掴んでいる縁を取り直し、その高さへ位置を合わせ直す
        //! @details 重力は当てない。縁の高さが変われば追い、失ったら手を放す
        //! @return 縁が続いている場合 true、それ以外の場合は false。false なら呼び出し側は即 return する
        [[nodiscard]] bool HoldLedge() noexcept;
        //! @brief 掴まりからジャンプの縦の初速を与えて落下へ移る
        //! @return ジャンプが押された場合 true、それ以外の場合は false。true なら呼び出し側は即 return する
        [[nodiscard]] bool LedgeJump() noexcept;
        //! 左右入力で縁に沿って動く。続いていない方向へは動かない
        void Shimmy(float dt) noexcept;
        //! よじ登りを始める。2 段補間の始点と終点を決めて登りの状態へ移る
        void ClimbLedge() noexcept;
        //! よじ登りの 1 フレーム。終われば通常移動へ戻す
        void UpdateLedgeClimb(float dt) noexcept;
        //! 手を放し、その場から落下させる
        void DropLedge() noexcept;
        //! 前入力が出ている場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldClimbLedge() const noexcept;
        //! 手放しのボタンが押された場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldDropLedge() const noexcept;

        //! 接地していて、走行入力が出ているか動いている場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldWalk() const noexcept;
        //! 接地していて走行も動きも無い場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldIdle() const noexcept;
        //! 接地を外れている場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldFall() const noexcept;
        //! 入力の向きと水平の速度の内積がブレーキのしきい値を下回る場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldBrake() const noexcept;
        //! スティックの倒し具合が遊び以上の場合 true、それ以外の場合は false
        [[nodiscard]] bool HasMoveInput() const noexcept;
        //! 水平の速さが k_Epsilon 未満の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsStopped() const noexcept;

        //! 自機だけの通知の受け口。基底の Events() は接地の 2 件を返すので名前を分ける
        [[nodiscard]] PlayerEvents& PlayerEventsRef() noexcept { return m_playerEvents; }

        //! 自分が持つ調整値。読む側はここから引く
        [[nodiscard]] const PlayerStats& Stats() const noexcept { return m_stats; }

        // setter は非有限値を書き込まない。重力や加速度へ入ると位置まで NaN が伝わる
        [[nodiscard]] float JumpImpulse() const noexcept { return m_stats.jumpImpulse; }
        void SetJumpImpulse(float value) noexcept;

        [[nodiscard]] float GravityUp() const noexcept { return m_stats.gravityUp; }
        void SetGravityUp(float value) noexcept;

        [[nodiscard]] float GravityDown() const noexcept { return m_stats.gravityDown; }
        void SetGravityDown(float value) noexcept;

        [[nodiscard]] float ApexHangVy() const noexcept { return m_stats.apexHangVy; }
        void SetApexHangVy(float value) noexcept;

        [[nodiscard]] float ApexHangScale() const noexcept { return m_stats.apexHangScale; }
        void SetApexHangScale(float value) noexcept;

        [[nodiscard]] float JumpReleaseScale() const noexcept { return m_stats.jumpReleaseScale; }
        void SetJumpReleaseScale(float value) noexcept;

        //! 接地を離れてもジャンプを受ける猶予秒
        [[nodiscard]] float CoyoteTime() const noexcept { return m_stats.coyoteTime; }
        void SetCoyoteTime(float value) noexcept;

        [[nodiscard]] float JumpBufferTime() const noexcept { return m_stats.jumpBufferTime; }
        void SetJumpBufferTime(float value) noexcept;

        [[nodiscard]] float WalkSpeed() const noexcept { return m_stats.walkSpeed; }
        void SetWalkSpeed(float value) noexcept;

        [[nodiscard]] float RunSpeed() const noexcept { return m_stats.runSpeed; }
        void SetRunSpeed(float value) noexcept;

        [[nodiscard]] float Acceleration() const noexcept { return m_stats.acceleration; }
        void SetAcceleration(float value) noexcept;

        [[nodiscard]] float AirAcceleration() const noexcept { return m_stats.airAcceleration; }
        void SetAirAcceleration(float value) noexcept;

        [[nodiscard]] float TurningDrag() const noexcept { return m_stats.turningDrag; }
        void SetTurningDrag(float value) noexcept;

        [[nodiscard]] float Friction() const noexcept { return m_stats.friction; }
        void SetFriction(float value) noexcept;

        [[nodiscard]] float Deceleration() const noexcept { return m_stats.deceleration; }
        void SetDeceleration(float value) noexcept;

        [[nodiscard]] float BrakeThreshold() const noexcept { return m_stats.brakeThreshold; }
        void SetBrakeThreshold(float value) noexcept;

        [[nodiscard]] float StickDeadzone() const noexcept { return m_stats.stickDeadzone; }
        void SetStickDeadzone(float value) noexcept;

        [[nodiscard]] float MaxStepHeight() const noexcept { return m_stats.maxStepHeight; }
        void SetMaxStepHeight(float value) noexcept;

        //! 掴める縁を探す帯の深さ。手の高さからこの距離だけ下まで見る
        [[nodiscard]] float LedgeGrabBelowHand() const noexcept { return m_stats.ledgeGrabBelowHand; }
        void SetLedgeGrabBelowHand(float value) noexcept;

        [[nodiscard]] float LedgeReach() const noexcept { return m_stats.ledgeReach; }
        void SetLedgeReach(float value) noexcept;

        [[nodiscard]] float LedgeClimbDuration() const noexcept { return m_stats.ledgeClimbDuration; }
        void SetLedgeClimbDuration(float value) noexcept;

        [[nodiscard]] float LedgeShimmySpeed() const noexcept { return m_stats.ledgeShimmySpeed; }
        void SetLedgeShimmySpeed(float value) noexcept;

        [[nodiscard]] float TurnSpeed() const noexcept { return m_stats.turnSpeed; }
        void SetTurnSpeed(float value) noexcept;

        [[nodiscard]] float BodySlamSpeed() const noexcept { return m_stats.bodySlamSpeed; }
        void SetBodySlamSpeed(float value) noexcept;

        [[nodiscard]] float BodySlamDistance() const noexcept { return m_stats.bodySlamDistance; }
        void SetBodySlamDistance(float value) noexcept;

        [[nodiscard]] float TapSlamSpeed() const noexcept { return m_stats.tapSlamSpeed; }
        void SetTapSlamSpeed(float value) noexcept;

        [[nodiscard]] float TapSlamUpSpeed() const noexcept { return m_stats.tapSlamUpSpeed; }
        void SetTapSlamUpSpeed(float value) noexcept;

        [[nodiscard]] float TapSlamDistance() const noexcept { return m_stats.tapSlamDistance; }
        void SetTapSlamDistance(float value) noexcept;

        [[nodiscard]] float SlamAimHoldTime() const noexcept { return m_stats.slamAimHoldTime; }
        void SetSlamAimHoldTime(float value) noexcept;

        [[nodiscard]] float SlamAimFadeTime() const noexcept { return m_stats.slamAimFadeTime; }
        void SetSlamAimFadeTime(float value) noexcept;

        //! 押したフレームの狙いを控える。離すまでの遅れのぶん、発動はこの向きから始める
        void MarkBodySlamAim() noexcept;

        //! 基底の OnStart に続けて、同居する状態機械を控える
        void OnStart() override;

        // 欄は登録される具象型に置く。リフレクションの直列化は自分の型の欄だけを回り、基底の鎖はたどらない
        NS_REFLECT_BEGIN(PlayerComponent, NS::Game::Entity::EntityComponent)
        NS_REFLECT_ACCESSOR(float, "ジャンプ初速", JumpImpulse(), SetJumpImpulse)
        NS_REFLECT_ACCESSOR(float, "上昇重力", GravityUp(), SetGravityUp)
        NS_REFLECT_ACCESSOR(float, "下降重力", GravityDown(), SetGravityDown)
        NS_REFLECT_ACCESSOR(float, "頂点滞空 Vy", ApexHangVy(), SetApexHangVy)
        NS_REFLECT_ACCESSOR(float, "頂点滞空倍率", ApexHangScale(), SetApexHangScale)
        NS_REFLECT_ACCESSOR(float, "ジャンプ離し倍率", JumpReleaseScale(), SetJumpReleaseScale)
        NS_REFLECT_ACCESSOR(float, "コヨーテ時間", CoyoteTime(), SetCoyoteTime)
        NS_REFLECT_ACCESSOR(float, "先行入力時間", JumpBufferTime(), SetJumpBufferTime)
        NS_REFLECT_ACCESSOR(float, "歩き速度", WalkSpeed(), SetWalkSpeed)
        NS_REFLECT_ACCESSOR(float, "走行速度", RunSpeed(), SetRunSpeed)
        NS_REFLECT_ACCESSOR(float, "加速度", Acceleration(), SetAcceleration)
        NS_REFLECT_ACCESSOR(float, "空中の加速度", AirAcceleration(), SetAirAcceleration)
        NS_REFLECT_ACCESSOR(float, "曲がる時の抵抗", TurningDrag(), SetTurningDrag)
        NS_REFLECT_ACCESSOR(float, "手を放した時の減速度", Friction(), SetFriction)
        NS_REFLECT_ACCESSOR(float, "ブレーキの減速度", Deceleration(), SetDeceleration)
        NS_REFLECT_ACCESSOR(float, "ブレーキのしきい値", BrakeThreshold(), SetBrakeThreshold)
        NS_REFLECT_ACCESSOR(float, "スティック遊び", StickDeadzone(), SetStickDeadzone)
        NS_REFLECT_ACCESSOR(float, "登れる段の高さ", MaxStepHeight(), SetMaxStepHeight)
        NS_REFLECT_ACCESSOR(float, "掴める縁の下向き距離", LedgeGrabBelowHand(), SetLedgeGrabBelowHand)
        NS_REFLECT_ACCESSOR(float, "縁へ手を伸ばす距離", LedgeReach(), SetLedgeReach)
        NS_REFLECT_ACCESSOR(float, "よじ登りの所要時間", LedgeClimbDuration(), SetLedgeClimbDuration)
        NS_REFLECT_ACCESSOR(float, "縁の横移動速度", LedgeShimmySpeed(), SetLedgeShimmySpeed)
        NS_REFLECT_ACCESSOR(float, "振り向きの速さ", TurnSpeed(), SetTurnSpeed)
        NS_REFLECT_ACCESSOR(float, "突進速度", BodySlamSpeed(), SetBodySlamSpeed)
        NS_REFLECT_ACCESSOR(float, "突進距離", BodySlamDistance(), SetBodySlamDistance)
        NS_REFLECT_ACCESSOR(float, "タップ初速", TapSlamSpeed(), SetTapSlamSpeed)
        NS_REFLECT_ACCESSOR(float, "タップの上向き初速", TapSlamUpSpeed(), SetTapSlamUpSpeed)
        NS_REFLECT_ACCESSOR(float, "タップ距離", TapSlamDistance(), SetTapSlamDistance)
        NS_REFLECT_ACCESSOR(float, "狙いの巻き戻し秒", SlamAimHoldTime(), SetSlamAimHoldTime)
        NS_REFLECT_ACCESSOR(float, "狙いの巻き戻しが消える秒", SlamAimFadeTime(), SetSlamAimFadeTime)
        NS_REFLECT_END()

    protected:
        //! 1 フレームの中身。状態機械を 1 つ進め、末尾でそのフレーム限りの入力を落とす
        void HandleStates(float dt) override;
        //! 稼働していない間もそのフレーム限りの入力は落とす。残すと再開した時に古い押下が効く
        void OnStepSkipped() override;

    private:
        //! 現在状態が通常移動 (立ち / 走り / 落下) の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsLocomotion() const noexcept;
        //! 突進を終える。水平の速さを走行の最高速度で切り、接地していれば走りへ、空中なら落下へ移す
        void EndBodySlam() noexcept;

        //! @brief 掴まり位置から掴める縁を探す
        //! @param[in] hangPos 手を伸ばす元になるカプセル中心の位置
        //! @param[out] outTop 見つけた縁の上端の y。見つからない場合は書き換えない
        //! @return 手の高さ以下の帯に縁があり、登り先も塞がっていない場合 true、それ以外の場合は false
        [[nodiscard]] bool FindLedgeTopAt(const NS::Core::Vector3& hangPos, float& outTop) const noexcept;

        //! 体当たりを出す水平の向き。入力・カメラの前・速度の順に見て、どれも無ければゼロ
        [[nodiscard]] NS::Core::Vector3 AimDirection() const noexcept;
        //! 控えた狙いを今の向きにどれだけ混ぜるか 0..1。巻き戻し秒までは 1、消える秒で 0
        [[nodiscard]] float BodySlamAimBlend01() const noexcept;

        NS::Core::Vector3 m_desiredDir{0.0f, 0.0f, 0.0f}; // 入力から作る world 空間の目標移動方向
        float m_desiredSpeedScale = 0.0f;                 // 目標速度スケール 0..1
        float m_climbRight = 0.0f;                        // 掴まり中の左右入力 -1..1
        float m_climbForward = 0.0f;                      // 掴まり中の前後入力 -1..1

        bool m_jumpHeld = false;                     // ジャンプボタン長押し中か
        bool m_prevJumpHeld = false;                 // 前のフレームの長押し状態
        bool m_jumpPressedThisFrame = false;         // このフレームでジャンプ押下があったか
        bool m_releaseLedgePressedThisFrame = false; // このフレームで手放しの押下があったか
        int m_jumpsRemaining = 1;                    // 残りジャンプ回数
        float m_coyoteTimer = 0.0f;                  // コヨーテ猶予の残り秒
        float m_bufferTimer = 0.0f;                  // 先行ジャンプ入力の残り秒

        float m_maxSpeed = 8.0f;

        float m_bodySlamBufferRemaining = 0.0f;               // 出せないフレームの押しを覚える残り秒
        bool m_bodySlamSpent = false;                         // 発動してから接地していないか
        bool m_bodySlamIsTap = false;                         // 溜め量 0 の飛び込みか
        float m_bodySlamRequestCharge01 = 0.0f;               // 要求された溜め量 0..1
        float m_bodySlamCharge01 = 0.0f;                      // 発動時に確定した溜め量 0..1
        float m_bodySlamTravelled = 0.0f;                     // 突進で進んだ水平距離
        float m_bodySlamDistanceTarget = 0.0f;                // 突進を終える水平距離
        bool m_bodySlamJustStarted = false;                   // 発動したフレームか
        NS::Core::Vector3 m_bodySlamDir{0.0f, 0.0f, 0.0f};    // 突進の水平の向き。正規化済み
        NS::Core::Vector3 m_bodySlamAimDir{0.0f, 0.0f, 0.0f}; // 押したフレームに控えた狙いの向き。正規化済み
        float m_bodySlamAimAge = 0.0f;                        // 狙いを控えてからの経過秒

        NS::Core::Vector3 m_facingDir{0.0f, 0.0f, 0.0f};       // 掴む向き。動こうとした水平の向きへ振り向きの速さで回る
        float m_lastMoveDistance = 0.0f;                       // 直前の Move で動いた距離。縁を探す帯の上の余白
        float m_ledgeTopY = 0.0f;                              // 掴んでいる縁の上端の y
        NS::Core::Vector3 m_ledgeFaceNormal{0.0f, 0.0f, 0.0f}; // 掴んでいる面の外向き法線
        NS::Core::Vector3 m_ledgeMantleStart{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 m_ledgeMantleEnd{0.0f, 0.0f, 0.0f};
        float m_ledgeMantleTimer = 0.0f; // よじ登りの経過秒

        PlayerStats m_stats;                                   // 調整値
        PlayerStateManagerComponent* m_stateManager = nullptr; // 状態機械 (非所有)

        PlayerEvents m_playerEvents;
    };
} // namespace NS::Game::Player
