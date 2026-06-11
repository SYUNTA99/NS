#include "Framework/Scene/Components/ThirdPersonFollowComponent.h"

#include "Framework/Core/Clock.h"
#include "Framework/Platform/Gamepad.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Mouse.h"
#include "Framework/Scene/Components/CameraComponent.h"
#include "Framework/Scene/Components/CharacterMovementComponent.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

#include <cmath>

namespace
{
    [[nodiscard]] float SpringApproach(float curr, float target, float omega, float dt) noexcept
    {
        if (omega <= 0.0f || dt <= 0.0f)
            return target;
        const float a = 1.0f - std::exp(-omega * dt);
        return curr + (target - curr) * a;
    }
} // namespace

namespace NS::Scene
{

    ThirdPersonFollowComponent::ThirdPersonFollowComponent(Transform* target) noexcept
        : Component(static_cast<int>(NS::Scene::TickPriority::Camera)), m_target(target)
    {}

    void ThirdPersonFollowComponent::SetTarget(Transform* target) noexcept
    {
        m_target = target;
    }
    void ThirdPersonFollowComponent::SetCamera(CameraComponent* camera) noexcept
    {
        m_camera = camera;
    }
    void ThirdPersonFollowComponent::SetInput(NS::Platform::Input* input) noexcept
    {
        m_input = input;
    }

    void ThirdPersonFollowComponent::SetMovement(const CharacterMovementComponent* movement) noexcept
    {
        m_movement = movement;
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

    void ThirdPersonFollowComponent::SetAutoDistances(float idle, float run, float jump) noexcept
    {
        if (idle > 0.0f)
            m_idleDistance = idle;
        if (run > 0.0f)
            m_runDistance = run;
        if (jump > 0.0f)
            m_jumpDistance = jump;
    }

    void ThirdPersonFollowComponent::SetRunSpeedThreshold(float speed) noexcept
    {
        m_runSpeedThreshold = speed;
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

    void ThirdPersonFollowComponent::OnUpdate()
    {
        const float dt = NS::Core::FrameTimer::FixedDelta();
        if (!IsActive() || m_camera == nullptr || m_target == nullptr || dt <= 0.0f)
            return;

        if (m_input != nullptr)
        {
            const auto& mouse = m_input->Mouse();
            const float mxSign = m_invertX ? -1.0f : 1.0f;
            const float mySign = m_invertY ? -1.0f : 1.0f;
            m_yaw += static_cast<float>(mouse.GetDeltaX()) * m_sensX * mxSign;
            m_pitch += static_cast<float>(mouse.GetDeltaY()) * m_sensY * mySign;

            const auto& pad = m_input->Gamepad(0);
            const NS::Platform::Stick rstick = pad.RightStick();
            m_yaw += rstick.x * m_stickSensX * dt * mxSign;
            m_pitch += rstick.y * m_stickSensY * dt * mySign;
        }

        m_pitch = NS::Math::Clamp(m_pitch, m_pitchMin, m_pitchMax);

        if (!m_manualDistance)
        {
            float desired = m_idleDistance;
            if (m_movement != nullptr)
            {
                if (!m_movement->IsGrounded())
                {
                    desired = m_jumpDistance;
                }
                else
                {
                    const auto v = m_movement->Velocity();
                    const float horiz = std::sqrt(v.x * v.x + v.z * v.z);
                    desired = (horiz > m_runSpeedThreshold) ? m_runDistance : m_idleDistance;
                }
            }
            m_desiredDistance = desired;
        }
        m_distance = SpringApproach(m_distance, m_desiredDistance, m_springOmega, dt);
    }

    void ThirdPersonFollowComponent::ApplyCameraTransform(float alpha) noexcept
    {
        if (!IsActive() || m_camera == nullptr || m_target == nullptr)
            return;

        const float cy = std::cos(m_yaw);
        const float sy = std::sin(m_yaw);
        const float cp = std::cos(m_pitch);
        const float sp = std::sin(m_pitch);
        const NS::Math::Vector3 forward{sy * cp, sp, cy * cp};

        // Player Mesh の補間と整合させ、相対位置のガタつきを防ぐ
        const NS::Math::Vector3 tgtPos = m_target->InterpolatedWorldMatrix(alpha).Translation();
        const NS::Math::Vector3 headPos{tgtPos.x, tgtPos.y + m_headHeight, tgtPos.z};
        const NS::Math::Vector3 camPos{
            headPos.x - forward.x * m_distance,
            headPos.y - forward.y * m_distance,
            headPos.z - forward.z * m_distance,
        };

        m_camera->SetPosition(camPos);
        m_camera->SetTarget(headPos);
        m_camera->SetUp({0.0f, 1.0f, 0.0f});
    }
} // namespace NS::Scene
