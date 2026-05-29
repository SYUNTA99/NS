#include "Framework/Scene/CharacterMovementComponent.h"

#include "Framework/Core/Clock.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Graphics/DebugDraw.h"
#include "Framework/Scene/ClimbableSurfaceComponent.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/PoleComponent.h"
#include "Framework/Scene/Transform.h"

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

    [[nodiscard]] NS::Core::Vector3 HorizontalSmooth(const NS::Core::Vector3& curr,
                                                     const NS::Core::Vector3& target,
                                                     float tau,
                                                     float dt) noexcept
    {
        return NS::Core::Vector3{
            SmoothApproach(curr.x, target.x, tau, dt),
            curr.y,
            SmoothApproach(curr.z, target.z, tau, dt),
        };
    }

    /// pole / fence 掴まり中の上下移動速度 (m/s)。 入力 1.0 で kClimbSpeed のレート。
    constexpr float kClimbSpeed = 2.0f;
    /// 離脱 jump 時、 接触面の逆方向に与える初速 (m/s)。
    constexpr float kClimbExitOutwardSpeed = 3.0f;
    /// 離脱 jump 時、 上方向に与える初速 (m/s)。
    constexpr float kClimbExitUpwardSpeed = 6.0f;
    /// auto-mantle 判定の上端余裕 (m)。 fence top または pole top にこの距離まで近づいたら歩行へ。
    constexpr float kClimbMantleEpsilon = 0.05f;
} // namespace

namespace NS::Scene
{
    CharacterMovementComponent::CharacterMovementComponent(NS::Scene::GameObject* owner) noexcept : Component(owner) {}

    void CharacterMovementComponent::SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept
    {
        m_desiredDir = worldDir;
        m_desiredSpeedScale = NS::Core::Clamp(speedScale01, 0.0f, 1.0f);
    }

    void CharacterMovementComponent::SetJumpPressed() noexcept
    {
        m_jumpPressedThisFrame = true;
    }

    void CharacterMovementComponent::SetJumpHeld(bool held) noexcept
    {
        m_jumpHeld = held;
    }

    void CharacterMovementComponent::SetCollisionWorld(std::span<const NS::Core::AABB> world)
    {
        m_collisionWorld.assign(world.begin(), world.end());
    }

    void CharacterMovementComponent::SetCollisionTriangles(std::span<const NS::Physics::Triangle> triangles)
    {
        m_collisionTriangles.assign(triangles.begin(), triangles.end());
    }

    void CharacterMovementComponent::SetClimbables(std::span<ClimbableSurfaceComponent* const> fences,
                                                   std::span<PoleComponent* const> poles) noexcept
    {
        m_fences = fences;
        m_poles = poles;
    }

    void CharacterMovementComponent::ResetState() noexcept
    {
        m_velocity = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_desiredDir = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        m_desiredSpeedScale = 0.0f;
        m_jumpHeld = false;
        m_prevJumpHeld = false;
        m_jumpPressedThisFrame = false;
        m_jumpsRemaining = 1;
        m_coyoteTimer = 0.0f;
        m_bufferTimer = 0.0f;
        m_wasGrounded = false;
        m_isGrounded = false;
        m_state = MovementState::Walking;
        m_attachedFence = nullptr;
        m_attachedPole = nullptr;
    }

    void CharacterMovementComponent::OnUpdate()
    {
        NS_SCOPED_TIMER(::NS::Core::LogCat::Game, "CharacterMovement::OnUpdate");

        const float dt = NS::Core::FrameTimer::FixedDelta();

        if (!IsActive() || dt <= 0.0f)
        {
            m_jumpPressedThisFrame = false;
            m_prevJumpHeld = m_jumpHeld;
            return;
        }

        // ClimbingPole / ClimbingFence では default CharacterController を bypass し、
        // pole の axis / fence の face plane に拘束された専用 update で position を直接更新する
        // (Mario-style non-physical controller)。
        if (m_state == MovementState::ClimbingPole)
        {
            // 離脱 jump: pole から outward (XZ 半径方向) + 上方向に飛び離れて Falling へ。
            if (m_jumpPressedThisFrame && m_attachedPole != nullptr)
            {
                const NS::Core::Vector3 pos = RootTransform().Position();
                const NS::Core::Vector3 axisStart = m_attachedPole->AxisStart();
                NS::Core::Vector3 outward{pos.x - axisStart.x, 0.0f, pos.z - axisStart.z};
                const float len = std::sqrt(outward.x * outward.x + outward.z * outward.z);
                if (len > 1e-4f)
                {
                    outward.x /= len;
                    outward.z /= len;
                }
                else
                {
                    outward = NS::Core::Vector3{1.0f, 0.0f, 0.0f};
                }
                m_velocity = NS::Core::Vector3{
                    outward.x * kClimbExitOutwardSpeed, kClimbExitUpwardSpeed, outward.z * kClimbExitOutwardSpeed};
                m_state = MovementState::Falling;
                m_attachedPole = nullptr;
                m_isGrounded = false;
                m_jumpPressedThisFrame = false;
                m_prevJumpHeld = m_jumpHeld;
                return;
            }

            if (m_attachedPole != nullptr)
            {
                NS::Core::Vector3 pos = RootTransform().Position();
                // 縦入力は m_desiredDir.z (前進入力) で代用する。
                // PlayerInputComponent は camera 相対 forward を z に積むので、 climb 中は
                // forward push = 上昇、 backward = 下降にマップする。
                const float verticalInput = m_desiredDir.z * m_desiredSpeedScale;
                pos.y += verticalInput * kClimbSpeed * dt;

                const NS::Core::Vector3 axisStart = m_attachedPole->AxisStart();
                const NS::Core::Vector3 axisEnd = m_attachedPole->AxisEnd();
                if (pos.y < axisStart.y)
                    pos.y = axisStart.y;

                // 上端に達したら自動で mantle (Walking) へ遷移。
                if (pos.y >= axisEnd.y - kClimbMantleEpsilon)
                {
                    pos.y = axisEnd.y;
                    RootTransform().SetPosition(pos);
                    m_state = MovementState::Walking;
                    m_attachedPole = nullptr;
                    m_velocity = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
                    m_isGrounded = true;
                    m_jumpsRemaining = 1;
                    m_jumpPressedThisFrame = false;
                    m_prevJumpHeld = m_jumpHeld;
                    return;
                }

                // XZ は pole 軸に snap して安定させる。
                pos.x = axisStart.x;
                pos.z = axisStart.z;
                RootTransform().SetPosition(pos);
                // velocity は climb logic が完全に支配する (gravity は無効、 controller も bypass)。
                m_velocity = NS::Core::Vector3{0.0f, verticalInput * kClimbSpeed, 0.0f};
            }

            m_skipControllerLastFrame = true;
            m_jumpPressedThisFrame = false;
            m_prevJumpHeld = m_jumpHeld;
            return;
        }

        if (m_state == MovementState::ClimbingFence)
        {
            // 離脱 jump: face normal の逆方向 + 上方向に飛び離れて Falling へ。
            if (m_jumpPressedThisFrame && m_attachedFence != nullptr)
            {
                const NS::Core::Vector3 n = m_attachedFence->FaceNormal();
                m_velocity = NS::Core::Vector3{
                    n.x * kClimbExitOutwardSpeed, kClimbExitUpwardSpeed, n.z * kClimbExitOutwardSpeed};
                m_state = MovementState::Falling;
                m_attachedFence = nullptr;
                m_isGrounded = false;
                m_jumpPressedThisFrame = false;
                m_prevJumpHeld = m_jumpHeld;
                return;
            }

            if (m_attachedFence != nullptr)
            {
                NS::Core::Vector3 pos = RootTransform().Position();
                // fence face plane 上の 2D 移動。 face normal n に対し up は world Y、
                // right は n × up で構築する。 入力 2D は (desiredDir.y, desiredDir.x) を up/right に積む。
                const NS::Core::Vector3 n = m_attachedFence->FaceNormal();
                const NS::Core::Vector3 worldUp{0.0f, 1.0f, 0.0f};
                NS::Core::Vector3 right{n.z, 0.0f, -n.x};
                const float rightLen = std::sqrt(right.x * right.x + right.z * right.z);
                if (rightLen > 1e-4f)
                {
                    right.x /= rightLen;
                    right.z /= rightLen;
                }

                const float vInput = m_desiredDir.y * m_desiredSpeedScale;
                const float hInput = m_desiredDir.x * m_desiredSpeedScale;
                pos.y += vInput * kClimbSpeed * dt;
                pos.x += right.x * hInput * kClimbSpeed * dt;
                pos.z += right.z * hInput * kClimbSpeed * dt;

                // fence 上端を超えたら mantle で Walking へ。
                const NS::Core::AABB fenceBox = m_attachedFence->WorldAABB();
                const float fenceTopY = fenceBox.Center.y + fenceBox.Extents.y;
                if (pos.y >= fenceTopY - kClimbMantleEpsilon)
                {
                    pos.y = fenceTopY;
                    RootTransform().SetPosition(pos);
                    m_state = MovementState::Walking;
                    m_attachedFence = nullptr;
                    m_velocity = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
                    m_isGrounded = true;
                    m_jumpsRemaining = 1;
                    m_jumpPressedThisFrame = false;
                    m_prevJumpHeld = m_jumpHeld;
                    return;
                }

                RootTransform().SetPosition(pos);
                m_velocity = NS::Core::Vector3{
                    right.x * hInput * kClimbSpeed, vInput * kClimbSpeed, right.z * hInput * kClimbSpeed};
            }

            m_skipControllerLastFrame = true;
            m_jumpPressedThisFrame = false;
            m_prevJumpHeld = m_jumpHeld;
            return;
        }

        // 通常 (Walking / Jumping / Falling): 既存の物理ロジックを温存。
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

        NS::Core::Vector3 targetHoriz{m_desiredDir.x * targetSpeed, 0.0f, m_desiredDir.z * targetSpeed};

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

        NS::Physics::CharacterControllerInput in{};
        in.position = RootTransform().Position();
        in.velocity = m_velocity;
        in.dt = dt;
        in.capsuleRadius = m_capsuleRadius;
        in.capsuleHalfHeight = m_capsuleHalfHeight;
        in.world = std::span<const NS::Core::AABB>(m_collisionWorld);
        in.worldTriangles = std::span<const NS::Physics::Triangle>(m_collisionTriangles);
        const NS::Physics::CharacterControllerResult out = m_controller.Update(in);

        RootTransform().SetPosition(out.position);
        m_velocity = out.velocity;
        m_wasGrounded = m_isGrounded;
        m_isGrounded = out.grounded;

        if (!m_wasGrounded && m_isGrounded)
            m_jumpsRemaining = 1;

        if (m_isGrounded)
            m_coyoteTimer = m_coyoteTime;

        // Walking / Jumping / Falling のサブ分類は high-level state の参考にする (controller bypass はしない)。
        if (m_isGrounded)
            m_state = MovementState::Walking;
        else if (m_velocity.y > 0.0f)
            m_state = MovementState::Jumping;
        else
            m_state = MovementState::Falling;

        // grab intent: 入力が掴まり面に向いていて、 かつ player 中心が trigger 内なら climb 状態へ。
        if (m_desiredSpeedScale > m_stickDeadzone)
        {
            const NS::Core::Vector3 pos = out.position;
            for (PoleComponent* pole : m_poles)
            {
                if (pole != nullptr && pole->ContainsPoint(pos))
                {
                    m_attachedPole = pole;
                    m_state = MovementState::ClimbingPole;
                    m_velocity = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
                    const NS::Core::Vector3 axisStart = pole->AxisStart();
                    RootTransform().SetPosition(NS::Core::Vector3{axisStart.x, pos.y, axisStart.z});
                    break;
                }
            }
            if (m_state != MovementState::ClimbingPole)
            {
                for (ClimbableSurfaceComponent* fence : m_fences)
                {
                    if (fence != nullptr && fence->ContainsPoint(pos))
                    {
                        m_attachedFence = fence;
                        m_state = MovementState::ClimbingFence;
                        m_velocity = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
                        break;
                    }
                }
            }
        }

        if (m_debugDraw)
        {
            const NS::Core::Vector3 center = out.position;
            const NS::Core::Vector3 axis{0.0f, m_capsuleHalfHeight, 0.0f};
            const NS::Core::Color color =
                m_isGrounded ? NS::Core::Color{0.2f, 1.0f, 0.2f, 1.0f} : NS::Core::Color{1.0f, 1.0f, 0.2f, 1.0f};
            NS::Graphics::DebugDraw::Capsule(center, axis, m_capsuleRadius, color);
        }

        m_prevJumpHeld = m_jumpHeld;
        m_jumpPressedThisFrame = false;
        m_skipControllerLastFrame = false;
    }
} // namespace NS::Scene
