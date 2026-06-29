#include "Framework/Scene/Components/EditorCameraComponent.h"

#include "Framework/Core/Clock.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"

#include <algorithm>
#include <cmath>

namespace NS::Scene
{

    EditorCameraComponent::EditorCameraComponent() noexcept
        : VirtualCameraComponent(static_cast<int>(TickPriority::Camera))
    {}

    void EditorCameraComponent::SetInput(NS::Platform::Input* input) noexcept
    {
        m_input = input;
    }

    void EditorCameraComponent::SetYawPitch(float yaw, float pitch) noexcept
    {
        m_yaw = yaw;
        m_pitch = std::clamp(pitch, kPitchMin, kPitchMax);
    }

    void EditorCameraComponent::SetCenter(NS::Math::Vector3 center) noexcept
    {
        m_center = center;
    }

    void EditorCameraComponent::SetDistance(float distance) noexcept
    {
        m_distance = std::clamp(distance, kMinDistance, kMaxDistance);
        m_desiredDistance = m_distance;
    }

    void EditorCameraComponent::ApplyOrbit(float yawDelta, float pitchDelta) noexcept
    {
        m_yaw += yawDelta;
        m_pitch = std::clamp(m_pitch + pitchDelta, kPitchMin, kPitchMax);
    }

    void EditorCameraComponent::ApplyPan(float panX, float panY) noexcept
    {
        // View space の right / up 軸に沿った平行移動。 感度は distance に比例
        const float sinYaw = std::sin(m_yaw);
        const float cosYaw = std::cos(m_yaw);
        const NS::Math::Vector3 right{cosYaw, 0.0f, -sinYaw};
        const NS::Math::Vector3 up{0.0f, 1.0f, 0.0f};
        const float scale = m_distance * 0.1f;
        m_center.x += (right.x * panX + up.x * panY) * scale;
        m_center.y += (right.y * panX + up.y * panY) * scale;
        m_center.z += (right.z * panX + up.z * panY) * scale;
    }

    void EditorCameraComponent::ApplyZoom(float zoomDelta) noexcept
    {
        // 加算でなく log scale にすることで近距離・遠距離の体感変化量を均等化する
        if (zoomDelta == 0.0f)
            return;
        const float factor = std::pow(0.9f, zoomDelta);
        m_desiredDistance = std::clamp(m_desiredDistance * factor, kMinDistance, kMaxDistance);
    }

    void EditorCameraComponent::ApplyKeyMove(float forwardAxis, float strafeAxis, float dt) noexcept
    {
        if (forwardAxis == 0.0f && strafeAxis == 0.0f)
            return;

        // yaw 向きの水平面に投影した forward / right。 pitch を無視するので見下ろしでも高さは変わらない
        const float sinYaw = std::sin(m_yaw);
        const float cosYaw = std::cos(m_yaw);
        const NS::Math::Vector3 forward{-sinYaw, 0.0f, -cosYaw};
        const NS::Math::Vector3 right{cosYaw, 0.0f, -sinYaw};
        const float step = m_keyMoveSpeed * m_distance * dt;
        m_center.x += (forward.x * forwardAxis + right.x * strafeAxis) * step;
        m_center.z += (forward.z * forwardAxis + right.z * strafeAxis) * step;
    }

    NS::Math::Vector3 EditorCameraComponent::ComputeCameraPosition() const noexcept
    {
        const float cosPitch = std::cos(m_pitch);
        const float sinPitch = std::sin(m_pitch);
        const float cosYaw = std::cos(m_yaw);
        const float sinYaw = std::sin(m_yaw);
        return {
            m_center.x + m_distance * cosPitch * sinYaw,
            m_center.y + m_distance * sinPitch,
            m_center.z + m_distance * cosPitch * cosYaw,
        };
    }

    void EditorCameraComponent::OnUpdate()
    {
        if (!IsActive())
            return;

        const float dt = NS::Core::FrameTimer::FixedDelta();

        // Mouse 入力。 UI がフォーカス中なら無視する
        const bool wantMouse = (m_input != nullptr) && m_input->UiWantsMouse();
        if (m_input != nullptr && !wantMouse)
        {
            auto& mouse = m_input->Mouse();
            if (mouse.IsHeld(NS::Platform::MouseButton::Right))
            {
                ApplyOrbit(static_cast<float>(mouse.GetDeltaX()) * m_mouseSensOrbit,
                           static_cast<float>(mouse.GetDeltaY()) * m_mouseSensOrbit);
            }
            if (mouse.IsHeld(NS::Platform::MouseButton::Middle))
            {
                ApplyPan(static_cast<float>(mouse.GetDeltaX()) * m_mouseSensPan,
                         static_cast<float>(mouse.GetDeltaY()) * m_mouseSensPan);
            }
            // GetWheelDelta は WHEEL_DELTA=120 単位なので /120 で 1 notch=1.0 に正規化
            ApplyZoom(static_cast<float>(mouse.GetWheelDelta()) / 120.0f * m_mouseSensZoom);
        }

        // Keyboard WASD。 UI がキー入力中なら無視する。 yaw に沿って水平面を平行移動し、 見下ろし角でも
        // 地面へ突っ込まず一定の高さで広域を流せるようにする
        if (m_input != nullptr && !m_input->UiWantsKeyboard())
        {
            auto& kb = m_input->Keyboard();
            float forwardAxis = 0.0f;
            float strafeAxis = 0.0f;
            if (kb.IsHeld(NS::Platform::Key::W))
                forwardAxis += 1.0f;
            if (kb.IsHeld(NS::Platform::Key::S))
                forwardAxis -= 1.0f;
            if (kb.IsHeld(NS::Platform::Key::D))
                strafeAxis += 1.0f;
            if (kb.IsHeld(NS::Platform::Key::A))
                strafeAxis -= 1.0f;
            ApplyKeyMove(forwardAxis, strafeAxis, dt);
        }

        // Gamepad は ImGui キャプチャ対象外、 常に入力する
        if (m_input != nullptr)
        {
            auto& gp = m_input->Gamepad(0);
            if (gp.IsConnected())
            {
                const auto rs = gp.RightStick();
                const auto ls = gp.LeftStick();
                ApplyOrbit(rs.x * m_padSensOrbit * dt, -rs.y * m_padSensOrbit * dt);
                ApplyPan(ls.x * m_padSensPan * dt, -ls.y * m_padSensPan * dt);
                ApplyZoom((gp.RightTrigger() - gp.LeftTrigger()) * m_padSensZoom * dt);
            }
        }

        // distance の critically-damped spring smoothing
        m_distance += (m_desiredDistance - m_distance) * std::min(1.0f, m_springOmega * dt);
    }

    CameraPose EditorCameraComponent::EvaluatePose(float) const noexcept
    {
        return MakePose(ComputeCameraPosition(), m_center, NS::Math::Vector3{0.0f, 1.0f, 0.0f});
    }

} // namespace NS::Scene
