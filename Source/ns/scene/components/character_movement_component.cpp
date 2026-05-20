#include "ns/scene/components/character_movement_component.h"

#include "ns/graphics/debug_draw.h"
#include "ns/scene/game_object.h"
#include "ns/scene/transform.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kHorizontalSpeedEpsilon = 0.01f;

    /// 一次遅れの離散化。tau = 時定数 (大きいほど鈍い)、dt = step。0 < tau で安定。
    [[nodiscard]] float SmoothApproach(float current, float target, float tau, float dt) noexcept
    {
        if (tau <= 0.0f)
            return target;
        const float a = 1.0f - std::exp(-dt / tau);
        return current + (target - current) * a;
    }

    [[nodiscard]] ns::core::Vector3 HorizontalSmooth(const ns::core::Vector3& curr,
                                                     const ns::core::Vector3& target,
                                                     float tau,
                                                     float dt) noexcept
    {
        return ns::core::Vector3{
            SmoothApproach(curr.x, target.x, tau, dt),
            curr.y,
            SmoothApproach(curr.z, target.z, tau, dt),
        };
    }
} // namespace

namespace ns::scene
{
    void CharacterMovementComponent::SetDesiredMove(const ns::core::Vector3& worldDir, float speedScale01) noexcept
    {
        m_desiredDir = worldDir;
        m_desiredSpeedScale = ns::core::Clamp(speedScale01, 0.0f, 1.0f);
    }

    void CharacterMovementComponent::SetJumpPressed() noexcept
    {
        m_jumpPressedThisFrame = true;
    }

    void CharacterMovementComponent::SetJumpHeld(bool held) noexcept
    {
        m_jumpHeld = held;
    }

    void CharacterMovementComponent::SetCollisionWorld(std::span<const ns::core::AABB> world)
    {
        m_collisionWorld.assign(world.begin(), world.end());
    }

    void CharacterMovementComponent::ResetState() noexcept
    {
        m_velocity = ns::core::Vector3{0.0f, 0.0f, 0.0f};
        m_desiredDir = ns::core::Vector3{0.0f, 0.0f, 0.0f};
        m_desiredSpeedScale = 0.0f;
        m_jumpHeld = false;
        m_prevJumpHeld = false;
        m_jumpPressedThisFrame = false;
        m_jumpsRemaining = 1;
        m_coyoteTimer = 0.0f;
        m_bufferTimer = 0.0f;
        m_wasGrounded = false;
        m_isGrounded = false;
    }

    void CharacterMovementComponent::OnUpdate(float dt)
    {
        if (!IsActive() || dt <= 0.0f)
        {
            m_jumpPressedThisFrame = false;
            m_prevJumpHeld = m_jumpHeld;
            return;
        }

        m_bufferTimer -= dt;
        if (m_jumpPressedThisFrame)
            m_bufferTimer = m_jumpBufferTime;

        const bool inAir = !m_isGrounded;
        if (inAir)
            m_coyoteTimer -= dt;

        float targetSpeed = 0.0f;
        if (m_desiredSpeedScale >= m_stickDeadzone)
        {
            targetSpeed = (m_desiredSpeedScale < 0.5f) ? m_walkSpeed : (m_maxSpeed * m_desiredSpeedScale);
        }

        ns::core::Vector3 targetHoriz{m_desiredDir.x * targetSpeed, 0.0f, m_desiredDir.z * targetSpeed};

        const float currHorizMag = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z);
        const float tau = (targetSpeed > currHorizMag + kHorizontalSpeedEpsilon) ? m_accelTau : m_decelTau;
        m_velocity = HorizontalSmooth(m_velocity, targetHoriz, tau, dt);

        const bool canGroundJump = (m_isGrounded || m_coyoteTimer > 0.0f) && m_jumpsRemaining > 0;
        const bool wantJump = m_jumpPressedThisFrame || m_bufferTimer > 0.0f;
        if (canGroundJump && wantJump)
        {
            m_velocity.y = m_jumpImpulse;
            --m_jumpsRemaining;
            m_bufferTimer = 0.0f;
            m_coyoteTimer = 0.0f;
        }
        else if (m_jumpPressedThisFrame && !m_isGrounded && m_coyoteTimer <= 0.0f && m_jumpsRemaining > 0)
        {
            m_velocity.y = m_jumpImpulse;
            --m_jumpsRemaining;
            m_bufferTimer = 0.0f;
        }

        if (m_prevJumpHeld && !m_jumpHeld && m_velocity.y > 0.0f)
            m_velocity.y *= m_jumpReleaseScale;

        const bool apex = std::abs(m_velocity.y) < m_apexHangVy;
        const float baseG = (m_velocity.y > 0.0f) ? m_gravityUp : m_gravityDown;
        const float g = apex ? baseG * m_apexHangScale : baseG;
        m_velocity.y += g * dt;

        ns::physics::CharacterControllerInput in{};
        in.position = RootTransform().Position();
        in.velocity = m_velocity;
        in.dt = dt;
        in.capsuleRadius = m_capsuleRadius;
        in.capsuleHalfHeight = m_capsuleHalfHeight;
        in.world = std::span<const ns::core::AABB>(m_collisionWorld);
        const ns::physics::CharacterControllerResult out = m_controller.Update(in);

        RootTransform().SetPosition(out.position);
        m_velocity = out.velocity;
        m_wasGrounded = m_isGrounded;
        m_isGrounded = out.grounded;

        if (!m_wasGrounded && m_isGrounded)
            m_jumpsRemaining = 1;

        if (m_isGrounded)
            m_coyoteTimer = m_coyoteTime;

        if (m_debugDraw)
        {
            const ns::core::Vector3 center = out.position;
            const ns::core::Vector3 axis{0.0f, m_capsuleHalfHeight, 0.0f};
            const ns::core::Color color =
                m_isGrounded ? ns::core::Color{0.2f, 1.0f, 0.2f, 1.0f} : ns::core::Color{1.0f, 1.0f, 0.2f, 1.0f};
            ns::graphics::DebugDraw::Capsule(center, axis, m_capsuleRadius, color);
        }

        m_prevJumpHeld = m_jumpHeld;
        m_jumpPressedThisFrame = false;
    }
} // namespace ns::scene
