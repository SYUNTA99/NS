#pragma once

/// @file CharacterMovementComponent.h
/// @brief Capsule + シングルジャンプ + coyote/buffer + asymmetric gravity + apex hang を保有する
///        Player 移動 Component。`NS::Physics::CharacterController` を value
///        member として内包し、毎 OnUpdate で desired velocity と dt を渡して結果を Root に適用する
///
/// gravity / jump など gameplay 値はここに保持し、CharacterController には数値計算のみを任せる
/// 責任分担。determinism 制約: OnUpdate(dt) で渡される fixed dt のみ使用、`NS::Core::FrameTimer::DeltaSeconds()`
/// 不可

#include "Framework/Math/Math.h"
#include "Framework/Physics/CharacterController.h"
#include "Framework/Scene/Component.h"

#include <span>
#include <vector>

namespace NS::Scene
{
    class PoleComponent;

    /// ClimbingPole / LedgeHanging / LedgeMantling の掴まり中は CharacterController を bypass して position
    /// を直更新する
    enum class MovementState
    {
        Walking,
        Jumping,
        Falling,
        ClimbingPole,
        LedgeHanging,
        LedgeMantling,
    };

    /// Player の物理状態を管理する Component。Input→desired velocity は PlayerInputComponent、衝突 world は
    /// SetPhysicsWorld で非所有借用する
    class CharacterMovementComponent : public Component
    {
    public:
        CharacterMovementComponent() noexcept;

        void SetDesiredMove(const NS::Math::Vector3& worldDir, float speedScale01) noexcept;

        /// 掴まり中の生ローカル入力で各成分は -1..1。SetDesiredMove と別チャンネル、前=登る マップ用
        void SetClimbMove(float localRight, float localForward) noexcept;

        void SetJumpPressed() noexcept;
        void SetJumpHeld(bool held) noexcept;

        /// pole 群を span で注入する。span のみ保存し、要素の寿命は呼出側の LevelPlayScene が保証する
        void SetClimbables(std::span<PoleComponent* const> poles) noexcept;

        /// 衝突 query 元の physics world を非所有で借用する。 非 null なら衝突計算をこの world へ委ねる
        void SetPhysicsWorld(const NS::Physics::PhysicsWorld* world) noexcept { m_world = world; }

        [[nodiscard]] MovementState State() const noexcept { return m_state; }
        /// テスト / 強制遷移用の setter。 通常は OnUpdate 内で遷移するため呼出不要
        void SetState(MovementState s) noexcept { m_state = s; }
        [[nodiscard]] PoleComponent* AttachedPole() const noexcept { return m_attachedPole; }

        [[nodiscard]] NS::Math::Vector3 Velocity() const noexcept { return m_velocity; }
        /// テスト / 外力用に速度を直接与える。 通常は OnUpdate 内で更新するため呼出不要
        void SetVelocity(const NS::Math::Vector3& v) noexcept { m_velocity = v; }
        [[nodiscard]] bool IsGrounded() const noexcept { return m_isGrounded; }
        [[nodiscard]] int JumpsRemaining() const noexcept { return m_jumpsRemaining; }

        void SetCapsuleRadius(float r) noexcept { m_capsuleRadius = (r < 0.001f) ? 0.001f : r; }
        void SetCapsuleHalfHeight(float h) noexcept { m_capsuleHalfHeight = (h < 0.001f) ? 0.001f : h; }
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
        NS_REFLECT_BEGIN(CharacterMovementComponent)
        NS_REFLECT_FIELD(m_jumpImpulse, "Jump Impulse")
        NS_REFLECT_FIELD(m_gravityUp, "Gravity Up")
        NS_REFLECT_FIELD(m_gravityDown, "Gravity Down")
        NS_REFLECT_FIELD(m_apexHangVy, "Apex Hang Vy")
        NS_REFLECT_FIELD(m_apexHangScale, "Apex Hang Scale")
        NS_REFLECT_FIELD(m_jumpReleaseScale, "Jump Release Scale")
        NS_REFLECT_FIELD(m_coyoteTime, "Coyote Time")
        NS_REFLECT_FIELD(m_jumpBufferTime, "Jump Buffer Time")
        NS_REFLECT_FIELD(m_maxSpeed, "Max Speed")
        NS_REFLECT_FIELD(m_walkSpeed, "Walk Speed")
        NS_REFLECT_FIELD(m_accelTau, "Accel Tau")
        NS_REFLECT_FIELD(m_decelTau, "Decel Tau")
        NS_REFLECT_FIELD(m_stickDeadzone, "Stick Deadzone")
        NS_REFLECT_END()

    private:
        /// 空中下降中に進行方向の block 縁を検出し、掴めれば LedgeHanging へ遷移して true を返す
        bool TryGrabLedge(const NS::Math::Vector3& pos) noexcept;

        /// LedgeHanging 中の毎フレーム更新。jump/後入力で即 mantle/drop、kLedgeMinHangTime 後のみ前入力で自動登り
        void UpdateLedgeHang(float dt) noexcept;

        /// LedgeMantling 中の毎フレーム更新。前半上昇/後半前進の 2 段補間で上面へ移動し、完了で Walking へ遷移
        void UpdateLedgeMantle(float dt) noexcept;

        /// 指定ぶら下がり位置で縁が同じ高さで続いているか。シミー先が端を越えていないか判定する
        [[nodiscard]] bool LedgeContinuesAt(const NS::Math::Vector3& hangPos) const noexcept;

        /// コヨーテ窓内ジャンプを 1 件記録する。上限超過時は最古を捨てる
        void PushCoyoteJumpMarker(const NS::Math::Vector3& edge, const NS::Math::Vector3& jump) noexcept;

        float m_gravityUp = -25.0f;
        float m_gravityDown = -35.0f;
        float m_apexHangVy = 1.0f;
        float m_apexHangScale = 0.5f;
        float m_jumpReleaseScale = 0.6f;
        float m_jumpImpulse = 12.0f;
        float m_coyoteTime = 0.025f;
        float m_jumpBufferTime = 0.25f;
        float m_maxSpeed = 8.0f;
        float m_walkSpeed = 4.0f;
        float m_stickDeadzone = 0.3f;
        float m_accelTau = 0.10f;
        float m_decelTau = 0.10f;

        float m_capsuleRadius = 0.4f;
        float m_capsuleHalfHeight = 0.5f;

        NS::Math::Vector3 m_velocity{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 m_desiredDir{0.0f, 0.0f, 0.0f};
        float m_desiredSpeedScale = 0.0f;
        float m_climbRight = 0.0f;
        float m_climbForward = 0.0f;

        bool m_jumpHeld = false;
        bool m_prevJumpHeld = false;
        bool m_jumpPressedThisFrame = false;
        int m_jumpsRemaining = 1;
        float m_coyoteTimer = 0.0f;
        float m_bufferTimer = 0.0f;
        bool m_wasGrounded = false;
        bool m_isGrounded = false;

        bool m_debugDraw = true;

        // 最後に接地していた world 位置。 縁を踏み外した直後はここが踏み外し点 すなわち縁になる
        NS::Math::Vector3 m_lastGroundedPosition{0.0f, 0.0f, 0.0f};
        // 表示中のコヨーテジャンプ記録。 寿命付きで OnUpdate 冒頭に減衰させ、 切れたら除外する
        std::vector<CoyoteJumpMarker> m_coyoteJumpMarkers;

        const NS::Physics::PhysicsWorld* m_world = nullptr;
        NS::Physics::CharacterController m_controller;

        MovementState m_state = MovementState::Walking;
        std::span<PoleComponent* const> m_poles{};
        PoleComponent* m_attachedPole = nullptr;
        bool m_skipControllerLastFrame = false;

        float m_ledgeTopY = 0.0f;
        NS::Math::Vector3 m_ledgeFaceNormal{0.0f, 0.0f, 0.0f};
        float m_ledgeRegrabCooldown = 0.0f;
        float m_ledgeHangTimer = 0.0f;

        NS::Math::Vector3 m_ledgeMantleStart{0.0f, 0.0f, 0.0f};
        NS::Math::Vector3 m_ledgeMantleEnd{0.0f, 0.0f, 0.0f};
        float m_ledgeMantleTimer = 0.0f;
    };
} // namespace NS::Scene
