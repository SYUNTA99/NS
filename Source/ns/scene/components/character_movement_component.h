#pragma once

/// @file character_movement_component.h
/// @brief Capsule + double jump + coyote/buffer + asymmetric gravity + apex hang を保有する
///        Player 移動 Component (, )。`ns::physics::CharacterController` を value
///        member として内包し、毎 OnUpdate で desired velocity と dt を渡して結果を Root に適用する。
///
/// gameplay 値 (gravity / jump など) はここに保持し、CharacterController には数値計算のみを任せる
/// ( 責任分担)。determinism 制約: OnUpdate(dt) で渡される fixed dt のみ使用、`Application::DeltaTime()` 不可。

#include "ns/core/math.h"
#include "ns/physics/character_controller.h"
#include "ns/scene/component.h"

#include <span>

namespace ns::scene
{
    /// Player の物理状態を管理する Component。Input → desired velocity の橋渡しは
    /// PlayerInputComponent が担う。collision world は MainScene が毎フレーム span で注入する。
    class CharacterMovementComponent : public Component
    {
    public:
        CharacterMovementComponent() noexcept = default;

        void SetDesiredMove(const ns::core::Vector3& worldDir, float speedScale01) noexcept;
        void SetJumpPressed() noexcept;
        void SetJumpHeld(bool held) noexcept;

        void SetCollisionWorld(std::span<const ns::core::AABB> world) noexcept;

        [[nodiscard]] ns::core::Vector3 Velocity() const noexcept { return m_velocity; }
        [[nodiscard]] bool IsGrounded() const noexcept { return m_isGrounded; }
        [[nodiscard]] int JumpsRemaining() const noexcept { return m_jumpsRemaining; }

        void SetCapsuleRadius(float r) noexcept { m_capsuleRadius = r; }
        void SetCapsuleHalfHeight(float h) noexcept { m_capsuleHalfHeight = h; }
        [[nodiscard]] float CapsuleRadius() const noexcept { return m_capsuleRadius; }
        [[nodiscard]] float CapsuleHalfHeight() const noexcept { return m_capsuleHalfHeight; }

        /// Debug 可視化の on/off。default true。CI / unit test では false 推奨。
        void SetDebugDrawEnabled(bool enabled) noexcept { m_debugDraw = enabled; }
        [[nodiscard]] bool IsDebugDrawEnabled() const noexcept { return m_debugDraw; }

        void OnUpdate(float dt) override;

    private:
        float m_gravityUp = -25.0f;
        float m_gravityDown = -35.0f;
        float m_apexHangVy = 1.0f;
        float m_apexHangScale = 0.5f;
        float m_jumpReleaseScale = 0.4f;
        float m_jumpImpulse = 9.0f;
        float m_coyoteTime = 0.10f;
        float m_jumpBufferTime = 0.10f;
        float m_maxSpeed = 8.0f;
        float m_walkSpeed = 4.0f;
        float m_stickDeadzone = 0.3f;
        float m_accelTau = 0.10f;
        float m_decelTau = 0.10f;

        float m_capsuleRadius = 0.4f;
        float m_capsuleHalfHeight = 0.5f;

        ns::core::Vector3 m_velocity{0.0f, 0.0f, 0.0f};
        ns::core::Vector3 m_desiredDir{0.0f, 0.0f, 0.0f};
        float m_desiredSpeedScale = 0.0f;

        bool m_jumpHeld = false;
        bool m_prevJumpHeld = false;
        bool m_jumpPressedThisFrame = false;
        int m_jumpsRemaining = 2;
        float m_coyoteTimer = 0.0f;
        float m_bufferTimer = 0.0f;
        bool m_wasGrounded = false;
        bool m_isGrounded = false;

        bool m_debugDraw = true;

        std::span<const ns::core::AABB> m_collisionWorld;
        ns::physics::CharacterController m_controller;
    };
} // namespace ns::scene
