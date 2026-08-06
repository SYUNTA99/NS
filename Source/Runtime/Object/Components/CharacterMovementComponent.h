#pragma once

#include "Runtime/Math/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/StateMachine.h"
#include "Runtime/Physics/CapsuleMover.h"

#include <span>
#include <string>
#include <vector>

namespace NS::Object
{
    /// @brief Player 移動の細分ラベル
    /// @details 実行の単位は States のデータで組む状態機械 (Locomotion / LedgeHang / LedgeMantle) で、
    /// この enum は Locomotion 内の歩き / ジャンプ / 落下まで割った読み取り用の細分。判定や演出が読む
    /// LedgeHanging / LedgeMantling の掴まり中は CapsuleMover を通さず position を直更新する
    enum class MovementState
    {
        Walking,
        Jumping,
        Falling,
        LedgeHanging,
        LedgeMantling,
    };

    /// @brief Player の物理状態を管理する Component
    /// @details Capsule + シングルジャンプ + coyote/buffer + 非対称重力 + apex hang を保持する
    /// NS::Physics::CapsuleMover を value member として内包する
    /// 毎 OnUpdate で desired velocity と dt を渡して結果を Root へ適用する
    /// gravity / jump など gameplay 値はここが持ち、CapsuleMover には数値計算だけ任せる
    /// Input→desired velocity は PlayerInputComponent が作る
    /// 衝突 world は OnStart で所属 scene から非所有借用する
    /// dt は NS::Core::FrameTimer::FixedDelta() のみで、DeltaSeconds() は使わない
    class CharacterMovementComponent : public Component
    {
    public:
        CharacterMovementComponent() noexcept;

        void SetDesiredMove(const NS::Math::Vector3& worldDir, float speedScale01) noexcept;

        /// 掴まり中の生ローカル入力で各成分は -1..1。SetDesiredMove と別チャンネル、前=登る マップ用
        void SetClimbMove(float localRight, float localForward) noexcept;

        void SetJumpPressed() noexcept;
        void SetJumpHeld(bool held) noexcept;

        /// 衝突 query 元の physics world を非所有で借用する。 scene 無しで動かすテスト用の継ぎ目で、
        /// 本編は OnStart が所属 scene の world を取る
        void SetPhysicsWorld(const NS::Physics::PhysicsWorld* world) noexcept { m_world = world; }

        /// 未注入なら所属 scene の衝突 world を借用する。world は scene が所有する実体のため
        /// level 再構築後もこの参照のまま有効
        void OnStart() override;

        [[nodiscard]] MovementState State() const noexcept { return m_state; }

        [[nodiscard]] NS::Math::Vector3 Velocity() const noexcept { return m_velocity; }
        /// テスト / 外力用に速度を直接与える。 通常は OnUpdate 内で更新するため呼出不要
        void SetVelocity(const NS::Math::Vector3& v) noexcept { m_velocity = v; }
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

        /// debug 可視化が縁の外側へ伸ばすコヨーテ帯の寸法に使う。 ライブ調整した値をそのまま反映する
        [[nodiscard]] float CoyoteTime() const noexcept { return m_coyoteTime; }
        [[nodiscard]] float MaxSpeed() const noexcept { return m_maxSpeed; }

        /// Debug 可視化の on/off。default true。CI / unit test では false 推奨
        void SetDebugDrawEnabled(bool enabled) noexcept { m_debugDraw = enabled; }
        [[nodiscard]] bool IsDebugDrawEnabled() const noexcept { return m_debugDraw; }

        /// コヨーテ窓内で跳んだ 1 件の記録。edge=最終接地位置, jump=跳躍位置, remaining=残り表示秒
        struct CoyoteJumpMarker
        {
            NS::Math::Vector3 edge{0.0f, 0.0f, 0.0f};
            NS::Math::Vector3 jump{0.0f, 0.0f, 0.0f};
            float remaining = 0.0f;
        };
        /// 生存中のコヨーテジャンプ記録。debug 描画が縁→跳躍点の赤線を引くのに読む。寿命切れは除外済
        [[nodiscard]] std::span<const CoyoteJumpMarker> CoyoteJumpMarkers() const noexcept
        {
            return m_coyoteJumpMarkers;
        }

        /// 奈落落ち復活などで状態を初期化する。velocity / grounded / jump 関連 timer を全リセット
        void ResetState() noexcept;

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
        NS_REFLECT_FIELD(m_maxSpeed, "最高速度")
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

        /// States のセミコロン区切りから状態機械を組む。全滅時は既定の並びへ退避する
        void BuildStates();

        /// 通常移動の 1 歩。歩き / ジャンプ / 落下を 1 本の物理パイプラインで進め、下降中に縁を探す
        void UpdateLocomotion(float dt) noexcept;
        /// 空中下降中に進行方向の block 縁を検出し、掴めれば LedgeHanging へ遷移して true を返す
        bool TryGrabLedge(const NS::Math::Vector3& pos) noexcept;

        /// LedgeHanging 中の毎フレーム更新。jump/後入力で即 mantle/drop、k_LedgeMinHangTime 後のみ前入力で自動登り
        void UpdateLedgeHang(float dt) noexcept;

        /// LedgeMantling 中の毎フレーム更新。前半上昇/後半前進の 2 段補間で上面へ移動し、完了で Walking へ遷移
        void UpdateLedgeMantle(float dt) noexcept;

        /// 指定ぶら下がり位置で縁が同じ高さで続いているか。シミー先が端を越えていないか判定する
        [[nodiscard]] bool LedgeContinuesAt(const NS::Math::Vector3& hangPos) const noexcept;

        /// コヨーテ窓内ジャンプを 1 件記録する。上限超過時は最古を捨てる
        void PushCoyoteJumpMarker(const NS::Math::Vector3& edge, const NS::Math::Vector3& jump) noexcept;

        float m_gravityUp = -25.0f;      // 上昇中の重力
        float m_gravityDown = -35.0f;    // 下降中の重力、上昇より強い
        float m_apexHangVy = 1.0f;       // apex とみなす縦速度のしきい値
        float m_apexHangScale = 0.5f;    // apex 付近で重力に掛ける倍率
        float m_jumpReleaseScale = 0.6f; // 上昇中に離した時の縦速度倍率
        float m_jumpImpulse = 12.0f;     // ジャンプ初速
        float m_coyoteTime = 0.025f;     // 接地を離れてもジャンプを受ける猶予秒
        float m_jumpBufferTime = 0.25f;  // 着地前の先行ジャンプ入力を覚える秒
        float m_maxSpeed = 8.0f;         // 最大移動速度
        float m_walkSpeed = 4.0f;        // 歩き速度
        float m_stickDeadzone = 0.3f;    // スティック入力のデッドゾーン
        float m_accelTau = 0.10f;        // 加速の時定数
        float m_decelTau = 0.10f;        // 減速の時定数

        float m_capsuleRadius = 0.4f;     // capsule 半径
        float m_capsuleHalfHeight = 0.5f; // capsule 半高

        NS::Math::Vector3 m_velocity{0.0f, 0.0f, 0.0f};   // 現在の速度
        NS::Math::Vector3 m_desiredDir{0.0f, 0.0f, 0.0f}; // 入力から作る world 空間の目標移動方向
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

        bool m_debugDraw = true; // debug 可視化の on/off

        // 最後に接地していた world 位置。 縁を踏み外した直後はここが踏み外し点 すなわち縁になる
        NS::Math::Vector3 m_lastGroundedPosition{0.0f, 0.0f, 0.0f};
        // 表示中のコヨーテジャンプ記録。 寿命付きで OnUpdate 冒頭に減衰させ、 切れたら除外する
        std::vector<CoyoteJumpMarker> m_coyoteJumpMarkers;

        const NS::Physics::PhysicsWorld* m_world = nullptr; // 衝突判定に使う physics world (非所有)
        NS::Physics::CapsuleMover m_controller;             // 数値計算を任せる controller

        // 状態の並び。セミコロン区切りの登録名で、先頭が初期状態。反映は組み直しから
        std::string m_stateNames = "Locomotion;LedgeHang;LedgeMantle";
        StateMachine<CharacterMovementComponent> m_machine; // States から組む状態機械。初回 OnUpdate で組む

        MovementState m_state = MovementState::Walking; // 細分ラベルの現在値

        float m_ledgeTopY = 0.0f;                              // 掴んだ縁の上端 Y
        NS::Math::Vector3 m_ledgeFaceNormal{0.0f, 0.0f, 0.0f}; // 掴んだ面の法線
        float m_ledgeRegrabCooldown = 0.0f;                    // 再掴み禁止の残り秒
        float m_ledgeHangTimer = 0.0f;                         // ぶら下がりの経過秒

        NS::Math::Vector3 m_ledgeMantleStart{0.0f, 0.0f, 0.0f}; // mantle 補間の始点
        NS::Math::Vector3 m_ledgeMantleEnd{0.0f, 0.0f, 0.0f};   // mantle 補間の終点
        float m_ledgeMantleTimer = 0.0f;                        // mantle の経過秒
    };
} // namespace NS::Object
