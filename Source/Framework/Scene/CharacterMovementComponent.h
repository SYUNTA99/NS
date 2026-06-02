#pragma once

/// @file CharacterMovementComponent.h
/// @brief Capsule + シングルジャンプ + coyote/buffer + asymmetric gravity + apex hang を保有する
///        Player 移動 Component。`NS::Physics::CharacterController` を value
///        member として内包し、毎 OnUpdate で desired velocity と dt を渡して結果を Root に適用する
///
/// gameplay 値 (gravity / jump など) はここに保持し、CharacterController には数値計算のみを任せる
/// 責任分担。determinism 制約: OnUpdate(dt) で渡される fixed dt のみ使用、`NS::Core::FrameTimer::DeltaSeconds()`
/// 不可

#include "Framework/Core/Math.h"
#include "Framework/Physics/CharacterController.h"
#include "Framework/Physics/SweptTriangle.h"
#include "Framework/Scene/Component.h"

#include <span>
#include <vector>

namespace NS::Scene
{
    class PoleComponent;

    /// Player 移動の高レベル state。 掴まり中 (ClimbingPole / LedgeHanging / LedgeMantling) は
    /// default CharacterController を bypass し、 掴んだ対象に拘束された専用ロジックで position を
    /// 直接更新する (Mario-style non-physical controller の locked constraint)
    /// LedgeHanging は専用パーツを使わず通常 block の AABB 縁にぶら下がる状態、 LedgeMantling は
    /// そこから上面へよじ登る数フレームのモーション中
    enum class MovementState
    {
        Walking,
        Jumping,
        Falling,
        ClimbingPole,
        LedgeHanging,
        LedgeMantling,
    };

    /// Player の物理状態を管理する Component。Input → desired velocity の橋渡しは
    /// PlayerInputComponent が担う。collision world は LevelEditorScene が毎フレーム span で注入する
    class CharacterMovementComponent : public Component
    {
    public:
        /// GameObject owner を受け取って auto-register するコンストラクタ
        explicit CharacterMovementComponent(NS::Scene::GameObject* owner) noexcept;

        void SetDesiredMove(const NS::Math::Vector3& worldDir, float speedScale01) noexcept;

        /// 掴まり中の縦横入力。 camera 回転をかける前の生ローカル入力 (前後=縦、 左右=横、 各 -1..1) を
        /// 受け取る。 通常移動の `SetDesiredMove` (camera 相対 world dir) とは別チャンネルで、
        /// pole の登りを camera 向きに依らず「前=登る」 にマップするための系統
        void SetClimbMove(float localRight, float localForward) noexcept;

        void SetJumpPressed() noexcept;
        void SetJumpHeld(bool held) noexcept;

        /// span を受け取って内部で owning std::vector にコピーする。呼出側 vector の lifetime に
        /// 依存させない (元 vector の reallocation / 破棄で dangling になる事故を防ぐ)
        void SetCollisionWorld(std::span<const NS::Math::AABB> world);

        /// Slope 用の世界座標 triangle 配列を受け取り、 内部 vector にコピーする
        void SetCollisionTriangles(std::span<const NS::Physics::Triangle> triangles);

        /// pole 群を non-owning span として外部から注入する。 span 自体だけ保存し、
        /// 要素 (Component pointer) の lifetime は呼出側 (LevelEditorScene) が保証する
        /// SetCollisionTriangles と同じく、毎フレーム scene 側で配列を組んで注入する
        void SetClimbables(std::span<PoleComponent* const> poles) noexcept;

        [[nodiscard]] MovementState State() const noexcept { return m_state; }
        /// テスト / 強制遷移用の setter。 通常は OnUpdate 内で遷移するため呼出不要
        void SetState(MovementState s) noexcept { m_state = s; }
        [[nodiscard]] PoleComponent* AttachedPole() const noexcept { return m_attachedPole; }

        [[nodiscard]] NS::Math::Vector3 Velocity() const noexcept { return m_velocity; }
        [[nodiscard]] bool IsGrounded() const noexcept { return m_isGrounded; }
        [[nodiscard]] int JumpsRemaining() const noexcept { return m_jumpsRemaining; }

        void SetCapsuleRadius(float r) noexcept { m_capsuleRadius = (r < 0.001f) ? 0.001f : r; }
        void SetCapsuleHalfHeight(float h) noexcept { m_capsuleHalfHeight = (h < 0.001f) ? 0.001f : h; }
        [[nodiscard]] float CapsuleRadius() const noexcept { return m_capsuleRadius; }
        [[nodiscard]] float CapsuleHalfHeight() const noexcept { return m_capsuleHalfHeight; }

        /// Debug 可視化の on/off。default true。CI / unit test では false 推奨
        void SetDebugDrawEnabled(bool enabled) noexcept { m_debugDraw = enabled; }
        [[nodiscard]] bool IsDebugDrawEnabled() const noexcept { return m_debugDraw; }

        /// 奈落落ち復活などで状態を初期化する。velocity / grounded / jump 関連 timer を全リセット
        void ResetState() noexcept;

        void OnUpdate() override;

    private:
        /// 空中下降中に進行方向の block 縁を検出し、 掴めれば LedgeHanging へ遷移する
        /// pos は controller 解決後の現在位置。 掴んだら true を返し、 state / 縁情報を更新する
        bool TryGrabLedge(const NS::Math::Vector3& pos) noexcept;

        /// LedgeHanging 中の毎フレーム更新。 jump / 後入力は即時 (mantle 開始 / drop)、 前入力での
        /// 自動登りは最小ぶら下がり時間 (kLedgeMinHangTime) を過ぎてから。 それ以外は縁に静止保持
        /// する (重力無効、 controller bypass)。 dt はタイマー積算用
        void UpdateLedgeHang(float dt) noexcept;

        /// LedgeMantling 中の毎フレーム更新。 ぶら下がり位置から上面の立ち位置へ、 前半上昇 /
        /// 後半前進の 2 段補間で動かし、 完了したら Walking (接地) へ遷移する。 dt は進行用
        void UpdateLedgeMantle(float dt) noexcept;

        /// 指定したぶら下がり位置に、 現在掴んでいるのと同じ高さの縁が続いているか。 シミー
        /// (縁沿い左右移動) 先が縁から外れていないか (端で止めるか) を判定する
        [[nodiscard]] bool LedgeContinuesAt(const NS::Math::Vector3& hangPos) const noexcept;

        float m_gravityUp = -25.0f;
        float m_gravityDown = -35.0f;
        float m_apexHangVy = 1.0f;
        float m_apexHangScale = 0.5f;
        float m_jumpReleaseScale = 0.6f;
        float m_jumpImpulse = 12.0f;
        float m_coyoteTime = 0.20f;
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

        std::vector<NS::Math::AABB> m_collisionWorld;
        std::vector<NS::Physics::Triangle> m_collisionTriangles;
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
