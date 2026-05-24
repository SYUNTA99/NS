#pragma once

/// @file CharacterMovementComponent.h
/// @brief Capsule + シングルジャンプ + coyote/buffer + asymmetric gravity + apex hang を保有する
///        Player 移動 Component (, )。`NS::Physics::CharacterController` を value
///        member として内包し、毎 OnUpdate で desired velocity と dt を渡して結果を Root に適用する。
///
/// gameplay 値 (gravity / jump など) はここに保持し、CharacterController には数値計算のみを任せる
/// ( 責任分担)。determinism 制約: OnUpdate(dt) で渡される fixed dt のみ使用、`Application::DeltaTime()` 不可。

#include "Framework/Core/Math.h"
#include "Framework/Physics/CharacterController.h"
#include "Framework/Scene/Component.h"

#include <span>
#include <vector>

namespace NS::Scene
{
    /// Player の物理状態を管理する Component。Input → desired velocity の橋渡しは
    /// PlayerInputComponent が担う。collision world は MainScene が毎フレーム span で注入する。
    class CharacterMovementComponent : public Component
    {
    public:
        /// GameObject owner を受け取って auto-register する ctor。
        explicit CharacterMovementComponent(NS::Scene::GameObject* owner) noexcept;

        void SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept;
        void SetJumpPressed() noexcept;
        void SetJumpHeld(bool held) noexcept;

        /// span を受け取って内部で owning std::vector にコピーする。呼出側 vector の lifetime に
        /// 依存させない (元 vector の reallocation / 破棄で dangling になる事故を防ぐ)。
        void SetCollisionWorld(std::span<const NS::Core::AABB> world);

        [[nodiscard]] NS::Core::Vector3 Velocity() const noexcept { return m_velocity; }
        [[nodiscard]] bool IsGrounded() const noexcept { return m_isGrounded; }
        [[nodiscard]] int JumpsRemaining() const noexcept { return m_jumpsRemaining; }

        void SetCapsuleRadius(float r) noexcept { m_capsuleRadius = (r < 0.001f) ? 0.001f : r; }
        void SetCapsuleHalfHeight(float h) noexcept { m_capsuleHalfHeight = (h < 0.001f) ? 0.001f : h; }
        [[nodiscard]] float CapsuleRadius() const noexcept { return m_capsuleRadius; }
        [[nodiscard]] float CapsuleHalfHeight() const noexcept { return m_capsuleHalfHeight; }

        /// Debug 可視化の on/off。default true。CI / unit test では false 推奨。
        void SetDebugDrawEnabled(bool enabled) noexcept { m_debugDraw = enabled; }
        [[nodiscard]] bool IsDebugDrawEnabled() const noexcept { return m_debugDraw; }

        /// 奈落落ち復活などで状態を初期化する。velocity / grounded / jump 関連 timer を全リセット。
        void ResetState() noexcept;

        void OnUpdate(float dt) override;

    private:
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

        NS::Core::Vector3 m_velocity{0.0f, 0.0f, 0.0f};
        NS::Core::Vector3 m_desiredDir{0.0f, 0.0f, 0.0f};
        float m_desiredSpeedScale = 0.0f;

        bool m_jumpHeld = false;
        bool m_prevJumpHeld = false;
        bool m_jumpPressedThisFrame = false;
        int m_jumpsRemaining = 1;
        float m_coyoteTimer = 0.0f;
        float m_bufferTimer = 0.0f;
        bool m_wasGrounded = false;
        bool m_isGrounded = false;

        bool m_debugDraw = true;

        std::vector<NS::Core::AABB> m_collisionWorld;
        NS::Physics::CharacterController m_controller;
    };
} // namespace NS::Scene
