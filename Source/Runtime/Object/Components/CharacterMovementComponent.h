#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/StateMachine.h"
#include "Runtime/Physics/CapsuleMover.h"

#include <span>
#include <string>
#include <vector>

namespace NS::Object
{
    //! @brief Player 移動の細分ラベル
    //! @details 実行の単位は m_stateNames から組む状態機械 (Locomotion / LedgeHang / LedgeMantle) で、
    //! この enum は Locomotion 内の歩き / ジャンプ / 落下まで割った読み取り用の細分
    //! LedgeHanging / LedgeMantling の掴まり中は CapsuleMover を通さず position を直更新する
    enum class MovementState
    {
        Walking,
        Jumping,
        Falling,
        LedgeHanging,
        LedgeMantling,
    };

    //! @brief Player の物理状態を管理する Component
    //! @details カプセル + 1 段ジャンプ + コヨーテ時間と先行入力 + 非対称重力 + 頂点滞空を保持する
    //! NS::Physics::CapsuleMover を実体で持つ
    //! Locomotion の 1 歩は速度と dt を CapsuleMover へ渡し、結果の位置を Root へ書く
    //! 重力やジャンプの調整値はここが持つ
    //! 目標の移動方向と速度スケールは PlayerInputComponent が入力から作る
    //! 衝突 world は OnStart で所属 scene から非所有で借りる
    //! dt は NS::Core::FrameTimer::FixedDelta() のみで、DeltaSeconds() は使わない
    class CharacterMovementComponent : public Component
    {
    public:
        CharacterMovementComponent() noexcept;

        //! world 空間の目標移動方向と速度スケールを渡す。 スケールは 0..1 に丸める
        void SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept;

        //! 直近に渡された目標速度スケール 0..1。走行入力が出ているかの判定に使う
        [[nodiscard]] float DesiredSpeedScale() const noexcept { return m_desiredSpeedScale; }

        //! 直近に渡された world 空間の目標移動方向。長さは入力の強さのままで正規化されていない
        [[nodiscard]] NS::Core::Vector3 DesiredDirection() const noexcept { return m_desiredDir; }

        //! 掴まり中の生ローカル入力で各成分は -1..1。SetDesiredMove とは別に持つ
        //! 前入力で登り、後入力で手を放す
        void SetClimbMove(float localRight, float localForward) noexcept;

        //! ジャンプの押下を 1 回ぶん立てる。OnUpdate の最後に落ちるので次のステップには残らない
        void SetJumpPressed() noexcept;
        //! ジャンプボタンの長押し状態を渡す。上昇中に離すと縦速度を縮めて上昇を切る
        void SetJumpHeld(bool held) noexcept;

        //! 衝突 query 元の physics world を非所有で借用する。 scene 無しで動かすテスト用の継ぎ目で、
        //! 本編は OnStart が所属 scene の world を取る
        void SetPhysicsWorld(const NS::Physics::PhysicsWorld* world) noexcept { m_world = world; }

        //! 未注入なら所属 scene の衝突 world を借用する。world は scene が所有する実体のため
        //! level 再構築後もこの参照のまま有効
        void OnStart() override;

        [[nodiscard]] MovementState State() const noexcept { return m_state; }

        [[nodiscard]] NS::Core::Vector3 Velocity() const noexcept { return m_velocity; }
        //! テスト / 外力用に速度を直接与える。 通常は OnUpdate 内で更新するため呼出不要
        void SetVelocity(const NS::Core::Vector3& v) noexcept { m_velocity = v; }
        [[nodiscard]] bool IsGrounded() const noexcept { return m_isGrounded; }
        [[nodiscard]] int JumpsRemaining() const noexcept { return m_jumpsRemaining; }

        void SetCapsuleRadius(float r) noexcept
        {
            if (r < 0.001f)
                m_capsuleRadius = 0.001f;
            else
                m_capsuleRadius = r;
        }
        void SetCapsuleHalfHeight(float h) noexcept
        {
            if (h < 0.001f)
                m_capsuleHalfHeight = 0.001f;
            else
                m_capsuleHalfHeight = h;
        }
        [[nodiscard]] float CapsuleRadius() const noexcept { return m_capsuleRadius; }
        [[nodiscard]] float CapsuleHalfHeight() const noexcept { return m_capsuleHalfHeight; }

        //! debug 可視化が縁の外側へ伸ばすコヨーテ帯の寸法に使う。 ライブ調整した値をそのまま反映する
        [[nodiscard]] float CoyoteTime() const noexcept { return m_coyoteTime; }
        [[nodiscard]] float MaxSpeed() const noexcept { return m_maxSpeed; }

        //! 最高速度を外から差し替える。負は 0 へ丸め、非有限値は書き込まない
        void SetMaxSpeed(float speed) noexcept;

        //! デバッグ可視化を切り替える。既定は true、テストでは false にする
        void SetDebugDrawEnabled(bool enabled) noexcept { m_debugDraw = enabled; }
        [[nodiscard]] bool IsDebugDrawEnabled() const noexcept { return m_debugDraw; }

        //! コヨーテ窓内で跳んだ 1 件の記録。edge=最終接地位置, jump=跳躍位置, remaining=残り表示秒
        struct CoyoteJumpMarker
        {
            NS::Core::Vector3 edge{0.0f, 0.0f, 0.0f};
            NS::Core::Vector3 jump{0.0f, 0.0f, 0.0f};
            float remaining = 0.0f;
        };
        //! 生存中のコヨーテジャンプ記録。debug 描画が縁→跳躍点の赤線を引くのに読む。寿命切れは除外済
        [[nodiscard]] std::span<const CoyoteJumpMarker> CoyoteJumpMarkers() const noexcept
        {
            return m_coyoteJumpMarkers;
        }

        //! 奈落落ち復活などで状態を初期化する。velocity / grounded / jump 関連 timer に加え、
        //! 掴まりの状態と状態機械も初期状態へ戻す
        void ResetState() noexcept;

        //! 状態機械を 1 歩ぶん進める。初回だけ m_stateNames から組み、1 歩限りの押下は最後に落とす
        void OnUpdate() override;

        // 操作感の調整値を Inspector へ公開する。 プレイ中にライブで触って感触を詰める用途
        NS_REFLECT_BEGIN(CharacterMovementComponent, Component)
        NS_REFLECT_FIELD(m_jumpImpulse, "ジャンプ初速")
        NS_REFLECT_FIELD(m_gravityUp, "上昇重力")
        NS_REFLECT_FIELD(m_gravityDown, "下降重力")
        NS_REFLECT_FIELD(m_apexHangVy, "頂点滞空 Vy")
        NS_REFLECT_FIELD(m_apexHangScale, "頂点滞空倍率")
        NS_REFLECT_FIELD(m_jumpReleaseScale, "ジャンプ離し倍率")
        NS_REFLECT_FIELD(m_coyoteTime, "コヨーテ時間")
        NS_REFLECT_FIELD(m_jumpBufferTime, "先行入力時間")
        NS_REFLECT_FIELD(m_walkSpeed, "歩き速度")
        NS_REFLECT_FIELD(m_accelTau, "加速時定数")
        NS_REFLECT_FIELD(m_decelTau, "減速時定数")
        NS_REFLECT_FIELD(m_stickDeadzone, "スティック遊び")
        NS_REFLECT_ACCESSOR(float, "カプセル半径", CapsuleRadius(), SetCapsuleRadius)
        NS_REFLECT_ACCESSOR(float, "カプセル半分の高さ", CapsuleHalfHeight(), SetCapsuleHalfHeight)
        NS_REFLECT_FIELD(m_debugDraw, "デバッグ表示")
        NS_REFLECT_FIELD(m_stateNames, "状態一覧")
        NS_REFLECT_END()

    private:
        friend class LocomotionState;
        friend class LedgeHangState;
        friend class LedgeMantleState;

        //! m_stateNames のセミコロン区切りから状態機械を組む。全滅時は既定の並びへ退避する
        void BuildStates();

        //! 通常移動の 1 歩。歩き / ジャンプ / 落下を 1 本の物理パイプラインで進め、下降中に縁を探す
        void UpdateLocomotion(float dt) noexcept;
        //! 空中下降中に進行方向の block 縁を検出し、掴めれば LedgeHanging へ遷移して true を返す
        bool TryGrabLedge(const NS::Core::Vector3& pos) noexcept;

        //! LedgeHanging 中の毎フレーム更新。jump/後入力で即 mantle/drop、k_LedgeMinHangTime 後のみ前入力で自動登り
        void UpdateLedgeHang(float dt) noexcept;

        //! LedgeMantling 中の毎フレーム更新。前半上昇/後半前進の 2 段補間で上面へ移動し、完了で Walking へ遷移
        void UpdateLedgeMantle(float dt) noexcept;

        //! 指定ぶら下がり位置で縁が同じ高さで続いているか。シミー先が端を越えていないか判定する
        [[nodiscard]] bool LedgeContinuesAt(const NS::Core::Vector3& hangPos) const noexcept;

        //! コヨーテ窓内ジャンプを 1 件記録する。上限超過時は最古を捨てる
        void PushCoyoteJumpMarker(const NS::Core::Vector3& edge, const NS::Core::Vector3& jump) noexcept;

        float m_gravityUp = -25.0f;      // 上昇中の重力
        float m_gravityDown = -35.0f;    // 下降中の重力、上昇より強い
        float m_apexHangVy = 1.0f;       // 頂点とみなす縦速度のしきい値
        float m_apexHangScale = 0.5f;    // 頂点付近で重力に掛ける倍率
        float m_jumpReleaseScale = 0.6f; // 上昇中に離した時の縦速度倍率
        float m_jumpImpulse = 12.0f;     // ジャンプ初速
        // 接地を離れてもジャンプを受ける猶予秒。実機プレイで詰めた約 1.5 フレームで、踏み外し直後のごく短い救済だけ残す
        float m_coyoteTime = 0.025f;
        // 着地前の先行ジャンプ入力を覚える秒
        // TODO: 暫定値。人の早押し誤差は概ね 100ms なので目標は 0.1 秒、体感で詰める
        float m_jumpBufferTime = 0.25f;
        float m_maxSpeed = 8.0f;      // 最大移動速度
        float m_walkSpeed = 4.0f;     // 歩き速度
        float m_stickDeadzone = 0.3f; // スティック入力のデッドゾーン
        float m_accelTau = 0.10f;     // 加速の時定数
        float m_decelTau = 0.10f;     // 減速の時定数

        float m_capsuleRadius = 0.4f;     // カプセル半径
        float m_capsuleHalfHeight = 0.5f; // カプセル半分の高さ

        NS::Core::Vector3 m_velocity{0.0f, 0.0f, 0.0f};   // 現在の速度
        NS::Core::Vector3 m_desiredDir{0.0f, 0.0f, 0.0f}; // 入力から作る world 空間の目標移動方向
        float m_desiredSpeedScale = 0.0f;                 // 目標速度スケール 0..1
        float m_climbRight = 0.0f;                        // 掴まり中の左右入力 -1..1
        float m_climbForward = 0.0f;                      // 掴まり中の前後入力 -1..1

        bool m_jumpHeld = false;             // ジャンプボタン長押し中か
        bool m_prevJumpHeld = false;         // 前フレームの長押し状態
        bool m_jumpPressedThisFrame = false; // このフレームでジャンプ押下があったか
        int m_jumpsRemaining = 1;            // 残りジャンプ回数
        float m_coyoteTimer = 0.0f;          // コヨーテ猶予の残り秒
        float m_bufferTimer = 0.0f;          // 先行ジャンプ入力の残り秒
        bool m_wasGrounded = false;          // 前フレームの接地状態
        bool m_isGrounded = false;           // 現在の接地状態

        bool m_debugDraw = true; // デバッグ可視化を出すか

        // 最後に接地していた world 位置。縁を踏み外した直後はここが縁の位置になる
        NS::Core::Vector3 m_lastGroundedPosition{0.0f, 0.0f, 0.0f};
        // 表示中のコヨーテジャンプ記録。 寿命付きで OnUpdate 冒頭に減衰させ、 切れたら除外する
        std::vector<CoyoteJumpMarker> m_coyoteJumpMarkers;

        const NS::Physics::PhysicsWorld* m_world = nullptr; // 衝突判定に使う physics world (非所有)
        NS::Physics::CapsuleMover m_controller;             // 数値計算を任せる controller

        // 状態の並び。セミコロン区切りの登録名で、先頭が初期状態。書き換えても組み直すまで効かない
        std::string m_stateNames = "Locomotion;LedgeHang;LedgeMantle";
        StateMachine<CharacterMovementComponent> m_machine; // m_stateNames から組む。初回 OnUpdate で組む

        MovementState m_state = MovementState::Walking; // 細分ラベルの現在値

        float m_ledgeTopY = 0.0f;                              // 掴んだ縁の上端 Y
        NS::Core::Vector3 m_ledgeFaceNormal{0.0f, 0.0f, 0.0f}; // 掴んだ面の法線
        float m_ledgeRegrabCooldown = 0.0f;                    // 再掴み禁止の残り秒
        float m_ledgeHangTimer = 0.0f;                         // ぶら下がりの経過秒

        NS::Core::Vector3 m_ledgeMantleStart{0.0f, 0.0f, 0.0f}; // mantle 補間の始点
        NS::Core::Vector3 m_ledgeMantleEnd{0.0f, 0.0f, 0.0f};   // mantle 補間の終点
        float m_ledgeMantleTimer = 0.0f;                        // mantle の経過秒
    };
} // namespace NS::Object
