#include "ns/scene/components/third_person_follow_component.h"

#include "ns/platform/gamepad.h"
#include "ns/platform/input.h"
#include "ns/platform/mouse.h"
#include "ns/scene/components/camera_component.h"
#include "ns/scene/components/character_movement_component.h"
#include "ns/scene/transform.h"

#include <cmath>

namespace
{
    constexpr float kRunSpeedThreshold = 4.0f;
    constexpr float kIdleDistance = 5.0f;
    constexpr float kRunDistance = 6.0f;
    constexpr float kJumpDistance = 7.0f;

    [[nodiscard]] float SpringApproach(float curr, float target, float omega, float dt) noexcept
    {
        if (omega <= 0.0f || dt <= 0.0f)
            return target;
        const float a = 1.0f - std::exp(-omega * dt);
        return curr + (target - curr) * a;
    }
} // namespace

namespace ns::scene
{
    ThirdPersonFollowComponent::ThirdPersonFollowComponent(Transform* target) noexcept : m_target(target) {}

    void ThirdPersonFollowComponent::SetTarget(Transform* target) noexcept
    {
        m_target = target;
    }
    void ThirdPersonFollowComponent::SetCamera(CameraComponent* camera) noexcept
    {
        m_camera = camera;
    }
    void ThirdPersonFollowComponent::SetInput(ns::platform::Input* input) noexcept
    {
        m_input = input;
    }

    void ThirdPersonFollowComponent::SetMovement(const CharacterMovementComponent* movement) noexcept
    {
        m_movement = movement;
    }

    void ThirdPersonFollowComponent::SetFovY(float radians) noexcept
    {
        m_fovY = radians;
        if (m_camera != nullptr)
            m_camera->SetFovY(radians);
    }

    void ThirdPersonFollowComponent::SetSensX(float radPerPixel) noexcept
    {
        m_sensX = radPerPixel;
    }
    void ThirdPersonFollowComponent::SetSensY(float radPerPixel) noexcept
    {
        m_sensY = radPerPixel;
    }
    void ThirdPersonFollowComponent::SetInvertX(bool invert) noexcept
    {
        m_invertX = invert;
    }
    void ThirdPersonFollowComponent::SetInvertY(bool invert) noexcept
    {
        m_invertY = invert;
    }

    void ThirdPersonFollowComponent::SetDistance(float distance) noexcept
    {
        m_distance = distance;
        m_desiredDistance = distance;
        m_manualDistance = true;
    }

    void ThirdPersonFollowComponent::ClearManualDistance() noexcept
    {
        m_manualDistance = false;
    }

    void ThirdPersonFollowComponent::OnUpdate(float dt)
    {
        if (!IsActive() || m_camera == nullptr || m_target == nullptr || dt <= 0.0f)
            return;

        if (m_input != nullptr)
        {
            const auto& mouse = m_input->Mouse();
            const float mxSign = m_invertX ? -1.0f : 1.0f;
            const float mySign = m_invertY ? -1.0f : 1.0f;
            m_yaw += static_cast<float>(mouse.DeltaX()) * m_sensX * mxSign;
            m_pitch += static_cast<float>(mouse.DeltaY()) * m_sensY * mySign;

            const auto& pad = m_input->Gamepad(0);
            const ns::platform::Stick rstick = pad.RightStick();
            m_yaw += rstick.x * m_stickSensX * dt * mxSign;
            m_pitch += rstick.y * m_stickSensY * dt * mySign;
        }

        m_pitch = ns::core::Clamp(m_pitch, m_pitchMin, m_pitchMax);

        if (!m_manualDistance)
        {
            float desired = kIdleDistance;
            if (m_movement != nullptr)
            {
                if (!m_movement->IsGrounded())
                {
                    desired = kJumpDistance;
                }
                else
                {
                    const auto v = m_movement->Velocity();
                    const float horiz = std::sqrt(v.x * v.x + v.z * v.z);
                    desired = (horiz > kRunSpeedThreshold) ? kRunDistance : kIdleDistance;
                }
            }
            m_desiredDistance = desired;
        }
        m_distance = SpringApproach(m_distance, m_desiredDistance, m_springOmega, dt);

        const float cy = std::cos(m_yaw);
        const float sy = std::sin(m_yaw);
        const float cp = std::cos(m_pitch);
        const float sp = std::sin(m_pitch);
        const ns::core::Vector3 forward{sy * cp, sp, cy * cp};

        const ns::core::Vector3 tgtPos = m_target->WorldMatrix().Translation();
        const ns::core::Vector3 headPos{tgtPos.x, tgtPos.y + m_headHeight, tgtPos.z};
        const ns::core::Vector3 camPos{
            headPos.x - forward.x * m_distance,
            headPos.y - forward.y * m_distance,
            headPos.z - forward.z * m_distance,
        };

        m_camera->SetPosition(camPos);
        m_camera->SetTarget(headPos);
        m_camera->SetUp({0.0f, 1.0f, 0.0f});
        m_camera->SetFovY(m_fovY);
    }
} // namespace ns::scene
