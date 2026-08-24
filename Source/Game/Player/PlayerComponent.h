#pragma once

#include "Game/Entity/EntityComponent.h"
#include "Game/Player/PlayerEvents.h"
#include "Game/Player/PlayerStats.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Reflection/Reflection.h"

#include <span>
#include <vector>

namespace NS::Game::Entity
{
    class EntityStateManagerComponent;
}

namespace NS::Game::Player
{
    class PlayerStateManagerComponent;
    class PlayerStatsManagerComponent;

    //! @brief 自機の能力を持つ Component
    //! @details 移動と接地は EntityComponent が持ち、ここには自機だけの能力と条件判定を置く
    //! 調整値は同居する PlayerStatsManagerComponent の組から読む。組が無ければ既定の組
    //! 状態は能力呼びの列だけにするので、動詞はすべて public
    //! 依存: NS::Game::Entity::EntityComponent / EntityStateManagerComponent, PlayerStatsManagerComponent
    class PlayerComponent : public NS::Game::Entity::EntityComponent
    {
    public:
        //! @brief コヨーテ窓内で跳んだ 1 件の記録
        struct CoyoteJumpMarker
        {
            NS::Core::Vector3 edge{0.0f, 0.0f, 0.0f}; // 最終接地位置
            NS::Core::Vector3 jump{0.0f, 0.0f, 0.0f}; // 跳んだ位置
            float remaining = 0.0f;                   // 残りの表示秒
        };

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

        //! ジャンプの押下を 1 回ぶん立てる。1 歩の終わりに落ちるので次の歩には残らない
        void SetJumpPressed() noexcept;
        //! ジャンプボタンの長押し状態を渡す。上昇中に離すと縦速度を縮める
        void SetJumpHeld(bool held) noexcept;
        [[nodiscard]] int JumpsRemaining() const noexcept { return m_jumpsRemaining; } //!< 残りジャンプ回数

        //! 接地を離れてもジャンプを受ける猶予秒。デバッグ可視化がコヨーテ帯の寸法に使う
        [[nodiscard]] float CoyoteTime() const noexcept;

        [[nodiscard]] float MaxSpeed() const noexcept { return m_maxSpeed; } //!< 走行の最高速度
        //! 最高速度を外から差し替える。負は 0 へ丸め、非有限値は書き込まない
        void SetMaxSpeed(float speed) noexcept;

        //! デバッグ可視化を切り替える。既定は true、テストでは false にする
        void SetDebugDrawEnabled(bool enabled) noexcept { m_debugDraw = enabled; }

        //! 生存中のコヨーテジャンプ記録。デバッグ描画が縁から跳躍点への線を引くのに読む。寿命切れは除外済
        [[nodiscard]] std::span<const CoyoteJumpMarker> CoyoteJumpMarkers() const noexcept
        {
            return m_coyoteJumpMarkers;
        }

        //! 奈落落ちの復活などで速度・接地・ジャンプまわりの記録と状態機械を初期状態へ戻す
        void ResetState() noexcept;

        //! 体当たりの状態の登録名。状態クラスの k_Name と同じ綴り
        static constexpr const char* k_BodySlamStateName = "BodySlam";
        //! 突進を終えた後に戻る状態の登録名
        static constexpr const char* k_IdleStateName = "Idle";
        //! 縁にぶら下がっている状態の登録名
        static constexpr const char* k_LedgeHangingStateName = "LedgeHanging";
        //! 縁から上面へよじ登っている状態の登録名
        static constexpr const char* k_LedgeClimbingStateName = "LedgeClimbing";

        //! 状態が次の登録名を渡す先。OnStep を呼ぶのが状態管理なので、状態の中では非 null
        [[nodiscard]] NS::Game::Entity::EntityStateManagerComponent* States() const noexcept;

        //! 体当たりの発動を要求する
        //! @details 溜め量 0 はタップの飛び込みで、非有限値は 0 とみなす。
        //! その歩で出せない要求は先行入力時間だけ覚え、過ぎたら失効する
        //! @param[in] charge01 溜め量 0..1
        void RequestBodySlam(float charge01) noexcept;
        //! 突進中の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsBodySlamming() const noexcept;
        //! 突進の進み具合 0..1。突進中でなければ 0
        [[nodiscard]] float BodySlamProgress01() const noexcept;
        [[nodiscard]] float BodySlamCharge01() const noexcept { return m_bodySlamCharge01; } //!< 発動時の溜め量 0..1
        //! 発動時の水平の速さ。衝突の威力の基礎になる
        [[nodiscard]] float BodySlamEntrySpeed() const noexcept { return m_bodySlamEntrySpeed; }
        //! 衝突の裁定が読む速度。突進中は向きと突進速度から作る
        //! @details 実速度は壁へ押し付けられた歩で 0 に潰れ、衝突の先読みが 1 歩も進まなくなる
        [[nodiscard]] NS::Core::Vector3 BodySlamVelocity() const noexcept;
        //! 突進を打ち切って通常移動へ戻す。突進中でなければ何もしない
        void CancelBodySlam() noexcept;

        //! 向きを解決して突進を始める。向きが決まらないか距離が 0 以下の場合 false、それ以外の場合は true
        //! @details 向きは 入力の水平 → カメラの水平前方 → 現在速度の水平 の順で解決する
        [[nodiscard]] bool BodySlam() noexcept;

        //! 突進の 1 歩を進める。距離を使い切るか進めない歩が続くと通常移動へ戻す
        void UpdateBodySlam(float dt) noexcept;

        // 移動の 1 歩を作る動詞。呼ぶ順序がそのまま手触りになる
        //! 先行入力とコヨーテ猶予のタイマーを 1 歩進める
        void TickTimers(float dt) noexcept;
        //! 入力の向きと強さから目標の水平速度を作り、一次遅れで近づける
        void AccelerateToInputDirection(float dt) noexcept;
        //! 接地かコヨーテ窓の内で押されていれば跳ぶ
        void Jump(float dt) noexcept;
        //! 上昇中にボタンを離した歩だけ縦速度を縮める
        void CutJumpRelease() noexcept;
        //! 上昇と下降で非対称な重力を当てる。頂点の近くは弱める
        void Gravity(float dt) noexcept;
        //! 着地でジャンプ回数を戻し、接地中はコヨーテ猶予と最終接地位置を張り直す
        void SyncGroundState() noexcept;
        //! 縁を掴めるか試す。掴んだ場合 true、それ以外の場合は false。true なら呼び出し側は即 return する
        [[nodiscard]] bool LedgeGrab() noexcept;

        //! ぶら下がりの経過秒を進め、縁の高さへ位置を貼り直す。重力は当てない
        void HoldLedge(float dt) noexcept;
        //! 左右入力で縁に沿って動く。続いていない方向へは動かない
        void Shimmy(float dt) noexcept;
        //! 移動先に同じ高さの縁が続いている場合 true、それ以外の場合は false
        [[nodiscard]] bool CanShimmyTo(const NS::Core::Vector3& hangPos) const noexcept;
        //! よじ登りを始める。2 段補間の始点と終点を決めて登りの状態へ移る
        void ClimbLedge() noexcept;
        //! よじ登りの 1 歩。終われば通常移動へ戻す
        void UpdateLedgeClimb(float dt) noexcept;
        //! 手を放す。面法線方向へ離して落下させ、再掴みをしばらく禁止する
        void DropLedge() noexcept;
        //! ジャンプ押下、または前入力が最小ぶら下がり時間を越えた場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldClimbLedge() const noexcept;
        //! 後入力がしきい値を越えた場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldDropLedge() const noexcept;

        //! 走行入力が出ているか動いている場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldWalk() const noexcept;
        //! 接地していて走行も動きも無い場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldIdle() const noexcept;
        //! 接地を外れている場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldFall() const noexcept;

        //! 自機だけの通知の受け口。基底の Events() は接地の 2 件を返すので名前を分ける
        [[nodiscard]] PlayerEvents& PlayerEventsRef() noexcept { return m_playerEvents; }

        //! 同居する組。無ければ既定の組。調整値はすべてここから読む
        [[nodiscard]] const PlayerStats& Stats() const noexcept;

        //! 基底の借用に続けて、同居する調整値の組と状態機械を控える
        void OnStart() override;

        // 欄は登録される具象型に置く。リフレクションの直列化は自分の型の欄だけを回り、基底の鎖はたどらない
        // 調整値 17 個は PlayerStatsManagerComponent が持つ
        NS_REFLECT_BEGIN(PlayerComponent, NS::Game::Entity::EntityComponent)
        NS_REFLECT_ACCESSOR(float, "カプセル半径", CapsuleRadius(), SetCapsuleRadius)
        NS_REFLECT_ACCESSOR(float, "カプセル半分の高さ", CapsuleHalfHeight(), SetCapsuleHalfHeight)
        NS_REFLECT_FIELD(m_debugDraw, "デバッグ表示")
        NS_REFLECT_END()

    protected:
        //! 1 歩の中身。状態機械へ 1 歩渡し、末尾で 1 歩限りの入力を落とす
        void HandleStates(float dt) override;
        //! 稼働していない歩でも 1 歩限りの入力は落とす。残すと再開した歩で古い押下が効く
        void OnStepSkipped() override;

    private:
        //! 現在状態が通常移動 (立ち / 走り / 落下) の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsLocomotion() const noexcept;
        //! コヨーテ窓内ジャンプを 1 件記録する。上限を超えた分は最古から捨てる
        void PushCoyoteJumpMarker(const NS::Core::Vector3& edge, const NS::Core::Vector3& jump) noexcept;

        NS::Core::Vector3 m_desiredDir{0.0f, 0.0f, 0.0f}; // 入力から作る world 空間の目標移動方向
        float m_desiredSpeedScale = 0.0f;                 // 目標速度スケール 0..1
        float m_climbRight = 0.0f;                        // 掴まり中の左右入力 -1..1
        float m_climbForward = 0.0f;                      // 掴まり中の前後入力 -1..1

        bool m_jumpHeld = false;             // ジャンプボタン長押し中か
        bool m_prevJumpHeld = false;         // 前の歩の長押し状態
        bool m_jumpPressedThisFrame = false; // この歩でジャンプ押下があったか
        int m_jumpsRemaining = 1;            // 残りジャンプ回数
        float m_coyoteTimer = 0.0f;          // コヨーテ猶予の残り秒
        float m_bufferTimer = 0.0f;          // 先行ジャンプ入力の残り秒

        // 走行中に MomentumComponent が毎歩書き換える。手で決める値ではないので調整値の組に入れない
        float m_maxSpeed = 8.0f;

        bool m_debugDraw = true; // デバッグ可視化を出すか

        float m_bodySlamBufferRemaining = 0.0f;            // 出せない歩の押しを覚える残り秒
        bool m_bodySlamIsTap = false;                      // 溜め量 0 の飛び込みか
        float m_bodySlamRequestCharge01 = 0.0f;            // 要求された溜め量 0..1
        float m_bodySlamCharge01 = 0.0f;                   // 発動時に確定した溜め量 0..1
        float m_bodySlamEntrySpeed = 0.0f;                 // 発動時の水平の速さ
        float m_bodySlamTravelled = 0.0f;                  // 突進で進んだ水平距離
        float m_bodySlamDistanceTarget = 0.0f;             // 突進を終える水平距離
        int m_bodySlamStallSteps = 0;                      // 進めなかった歩の連続数
        NS::Core::Vector3 m_bodySlamDir{0.0f, 0.0f, 0.0f}; // 突進の水平の向き。正規化済み

        float m_ledgeTopY = 0.0f;                              // 掴んでいる縁の上端の y
        NS::Core::Vector3 m_ledgeFaceNormal{0.0f, 0.0f, 0.0f}; // 掴んでいる面の外向き法線
        float m_ledgeRegrabCooldown = 0.0f;                    // 再掴みを禁止する残り秒
        float m_ledgeHangTimer = 0.0f;                         // 掴んでからの経過秒
        NS::Core::Vector3 m_ledgeMantleStart{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 m_ledgeMantleEnd{0.0f, 0.0f, 0.0f};
        float m_ledgeMantleTimer = 0.0f; // よじ登りの経過秒

        // 最後に接地していた world 位置。縁を踏み外した直後はここが縁の位置になる
        NS::Core::Vector3 m_lastGroundedPosition{0.0f, 0.0f, 0.0f};
        std::vector<CoyoteJumpMarker> m_coyoteJumpMarkers; // 表示中のコヨーテジャンプ記録。寿命付き

        PlayerStatsManagerComponent* m_statsManager = nullptr; // 調整値の組 (非所有)
        PlayerStateManagerComponent* m_stateManager = nullptr; // 状態機械 (非所有)

        PlayerEvents m_playerEvents;
    };
} // namespace NS::Game::Player
