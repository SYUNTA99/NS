#include "Editor/EditorCamera.h"

#include "Runtime/Core/Clock.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Keyboard.h"

#include <algorithm>
#include <cmath>

namespace NS::Editor
{

    void EditorCamera::SetYawPitch(float yaw, float pitch) noexcept
    {
        m_yaw = yaw;
        m_pitch = std::clamp(pitch, k_PitchMin, k_PitchMax);
    }

    void EditorCamera::SetCenter(NS::Math::Vector3 center) noexcept
    {
        m_center = center;
    }

    void EditorCamera::SetDistance(float distance) noexcept
    {
        m_distance = std::clamp(distance, k_MinDistance, k_MaxDistance);
        m_desiredDistance = m_distance;
    }

    void EditorCamera::ApplyOrbit(float yawDelta, float pitchDelta) noexcept
    {
        m_yaw += yawDelta;
        m_pitch = std::clamp(m_pitch + pitchDelta, k_PitchMin, k_PitchMax);
    }

    void EditorCamera::ApplyPan(float panX, float panY) noexcept
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

    void EditorCamera::ApplyZoom(float zoomDelta) noexcept
    {
        // 加算でなく log scale にして、近距離・遠距離の体感変化量を揃える
        if (zoomDelta == 0.0f)
            return;
        const float factor = std::pow(0.9f, zoomDelta);
        m_desiredDistance = std::clamp(m_desiredDistance * factor, k_MinDistance, k_MaxDistance);
    }

    void EditorCamera::ApplyLook(float yawDelta, float pitchDelta) noexcept
    {
        // eye を固定して回すため、 回転前後の eye 差を center へ戻す。 これで orbit でなくその場の見回しになる
        const NS::Math::Vector3 eyeBefore = ComputeCameraPosition();
        m_yaw += yawDelta;
        m_pitch = std::clamp(m_pitch + pitchDelta, k_PitchMin, k_PitchMax);
        const NS::Math::Vector3 eyeAfter = ComputeCameraPosition();
        m_center.x += eyeBefore.x - eyeAfter.x;
        m_center.y += eyeBefore.y - eyeAfter.y;
        m_center.z += eyeBefore.z - eyeAfter.z;
    }

    void EditorCamera::ApplyFlyMove(
        float forwardAxis, float strafeAxis, float verticalAxis, float dt, float speedScale) noexcept
    {
        if (forwardAxis == 0.0f && strafeAxis == 0.0f && verticalAxis == 0.0f)
            return;

        const float cosPitch = std::cos(m_pitch);
        const float sinPitch = std::sin(m_pitch);
        const float sinYaw = std::sin(m_yaw);
        const float cosYaw = std::cos(m_yaw);
        // 視線方向 forward = normalize(center - eye)。 LH look-at の前方で pitch を含むので見ている方向へ進める
        const NS::Math::Vector3 forward{-cosPitch * sinYaw, -sinPitch, -cosPitch * cosYaw};
        // 画面右 right = cross(worldUp, forward)。 LH なので yaw=0 で -X。 旧実装の +X とは逆で、 左右反転を解消する
        const NS::Math::Vector3 right{-cosYaw, 0.0f, sinYaw};
        // 速さは distance 比例のままだが、 寄った時に動けなくならないよう距離に下限を置く
        // 立方体 1 個へ注視すると distance は下限の 2m まで落ち、 比例のままでは 1.2m/s と歩くより遅い
        const float step = m_keyMoveSpeed * std::max(m_distance, k_MinMoveDistance) * dt * speedScale;
        m_center.x += (forward.x * forwardAxis + right.x * strafeAxis) * step;
        m_center.y += (forward.y * forwardAxis + verticalAxis) * step;
        m_center.z += (forward.z * forwardAxis + right.z * strafeAxis) * step;
    }

    void EditorCamera::ApplyInput(const EditorCameraInput& input) noexcept
    {
        if (input.flying)
        {
            ApplyLook(input.lookYawPixels * m_mouseSensOrbit, input.lookPitchPixels * m_mouseSensOrbit);
        }
        if (input.panXPixels != 0.0f || input.panYPixels != 0.0f)
        {
            ApplyPan(input.panXPixels * m_mouseSensPan, input.panYPixels * m_mouseSensPan);
        }
        ApplyZoom(input.wheelNotches * m_mouseSensZoom);
        if (input.flying)
        {
            ApplyFlyMove(input.forwardAxis, input.strafeAxis, input.verticalAxis, input.deltaSeconds, input.speedScale);
        }

        // distance を臨界減衰バネで目標距離へ滑らかに寄せる
        m_distance += (m_desiredDistance - m_distance) * std::min(1.0f, m_springOmega * input.deltaSeconds);
    }

    NS::Math::Vector3 EditorCamera::ComputeCameraPosition() const noexcept
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

    void EditorCamera::Tick() noexcept
    {
        const float dt = NS::Core::FrameTimer::FixedDelta();
        auto& input = NS::Platform::Input::Get();

        // Platform Input から 1 フレーム分の free-fly 入力を組む。 感度適用とバネは注入する側に任せる
        EditorCameraInput frameInput{};
        frameInput.deltaSeconds = dt;

        // マウス入力: 右ドラッグで見回し、中ドラッグで pan、ホイールで zoom
        const bool wantMouse = input.UiWantsMouse();
        if (!wantMouse)
        {
            auto& mouse = input.Mouse();
            // 右ドラッグ中はその場で見回すフライ視点。 eye 固定で回し、 WASD/QE の移動も許可する
            frameInput.flying = mouse.IsHeld(NS::Platform::MouseButton::Right);
            if (frameInput.flying)
            {
                frameInput.lookYawPixels = static_cast<float>(mouse.GetDeltaX());
                frameInput.lookPitchPixels = static_cast<float>(mouse.GetDeltaY());
            }
            if (mouse.IsHeld(NS::Platform::MouseButton::Middle))
            {
                frameInput.panXPixels = static_cast<float>(mouse.GetDeltaX());
                frameInput.panYPixels = static_cast<float>(mouse.GetDeltaY());
            }
            // GetWheelDelta は WHEEL_DELTA=120 単位なので /120 で 1 刻み=1.0 に正規化
            frameInput.wheelNotches = static_cast<float>(mouse.GetWheelDelta()) / 120.0f;
        }

        // 右ドラッグ中のみ WASD で視線方向へフライ、 Q E で world 上下する。 UI がキー入力中なら無視する
        if (frameInput.flying && !input.UiWantsKeyboard())
        {
            auto& kb = input.Keyboard();
            if (kb.IsHeld(NS::Platform::Key::W))
                frameInput.forwardAxis += 1.0f;
            if (kb.IsHeld(NS::Platform::Key::S))
                frameInput.forwardAxis -= 1.0f;
            if (kb.IsHeld(NS::Platform::Key::D))
                frameInput.strafeAxis += 1.0f;
            if (kb.IsHeld(NS::Platform::Key::A))
                frameInput.strafeAxis -= 1.0f;
            if (kb.IsHeld(NS::Platform::Key::E))
                frameInput.verticalAxis += 1.0f;
            if (kb.IsHeld(NS::Platform::Key::Q))
                frameInput.verticalAxis -= 1.0f;
            // Shift 押下中は 4 倍速で移動する。 通常は等倍で寄せて微調整し、 Shift で広い地形を一気に移動する
            if (kb.IsHeld(NS::Platform::Key::Shift))
            {
                frameInput.speedScale = 4.0f;
            }
        }

        // Gamepad は ImGui キャプチャ対象外、 常に入力する。 free-fly 入力とは別枠でその場に適用する
        {
            auto& gp = input.Gamepad(0);
            if (gp.IsConnected())
            {
                const auto rs = gp.RightStick();
                const auto ls = gp.LeftStick();
                ApplyOrbit(rs.x * m_padSensOrbit * dt, -rs.y * m_padSensOrbit * dt);
                ApplyPan(ls.x * m_padSensPan * dt, -ls.y * m_padSensPan * dt);
                ApplyZoom((gp.RightTrigger() - gp.LeftTrigger()) * m_padSensZoom * dt);
            }
        }

        ApplyInput(frameInput);
    }

    NS::Object::CameraPose EditorCamera::Pose() const noexcept
    {
        NS::Object::CameraPose pose{};
        pose.position = ComputeCameraPosition();
        pose.target = m_center;
        pose.up = NS::Math::Vector3{0.0f, 1.0f, 0.0f};
        pose.fovY = m_fovY;
        pose.nearPlane = m_nearPlane;
        pose.farPlane = m_farPlane;
        return pose;
    }

} // namespace NS::Editor
