#pragma once

#include "Game/Entity/EntityComponent.h"
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

        // TODO: 突進を移すまで定義が無い。呼ぶとリンクで落ちる
        void RequestBodySlam(float charge01) noexcept;
        [[nodiscard]] bool IsBodySlamming() const noexcept;
        [[nodiscard]] float BodySlamProgress01() const noexcept;
        [[nodiscard]] float BodySlamCharge01() const noexcept;
        [[nodiscard]] float BodySlamEntrySpeed() const noexcept;
        [[nodiscard]] NS::Core::Vector3 BodySlamVelocity() const noexcept;
        void CancelBodySlam() noexcept;

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

        //! 走行入力が出ているか動いている場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldWalk() const noexcept;
        //! 接地していて走行も動きも無い場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldIdle() const noexcept;
        //! 接地を外れている場合 true、それ以外の場合は false
        [[nodiscard]] bool ShouldFall() const noexcept;

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
        //! 1 歩の中身。末尾で 1 歩限りの入力を落とす
        void HandleStates(float dt) override;
        //! 稼働していない歩でも 1 歩限りの入力は落とす。残すと再開した歩で古い押下が効く
        void OnStepSkipped() override;

    private:
        // TODO: 状態機械へ差し替えるまでの 1 本道。動詞を現行と同じ並びで呼ぶ
        void StepLocomotion(float dt) noexcept;
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

        // 最後に接地していた world 位置。縁を踏み外した直後はここが縁の位置になる
        NS::Core::Vector3 m_lastGroundedPosition{0.0f, 0.0f, 0.0f};
        std::vector<CoyoteJumpMarker> m_coyoteJumpMarkers; // 表示中のコヨーテジャンプ記録。寿命付き

        PlayerStatsManagerComponent* m_statsManager = nullptr;                   // 調整値の組 (非所有)
        NS::Game::Entity::EntityStateManagerComponent* m_stateManager = nullptr; // 状態機械 (非所有)
    };
} // namespace NS::Game::Player
