#include "Editor/EditorCamera.h"

#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Core/Windows/Keyboard.h"

#include <algorithm>
#include <cmath>

namespace NS::Editor
{
        void EditorCamera::SetCenter(Vector3 center) noexcept { m_center = center; }

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
            // yaw に合わせた横移動と世界の上下移動。距離に比例して移動量を変える
            const float sinYaw = std::sin(m_yaw);
            const float cosYaw = std::cos(m_yaw);
            const Vector3 right{cosYaw, 0.0f, -sinYaw};
            const Vector3 up{0.0f, 1.0f, 0.0f};
            const float scale = m_distance * 0.1f;
            m_center.x += (right.x * panX + up.x * panY) * scale;
            m_center.y += (right.y * panX + up.y * panY) * scale;
            m_center.z += (right.z * panX + up.z * panY) * scale;
        }

        void EditorCamera::ApplyZoom(float zoomDelta) noexcept
        {
            // 加算でなく log scale にして、近距離・遠距離の体感変化量を揃える
            if (zoomDelta == 0.0f)
            {
                return;
            }
            const float factor = std::pow(0.9f, zoomDelta);
            m_desiredDistance = std::clamp(m_desiredDistance * factor, k_MinDistance, k_MaxDistance);
        }

        void EditorCamera::ApplyLook(float yawDelta, float pitchDelta) noexcept
        {
            // eye を固定して回すため、回転前後の eye 差を center へ戻す。これで orbit でなくその場の見回しになる
            const Vector3 eyeBefore = ComputeCameraPosition();
            m_yaw += yawDelta;
            m_pitch = std::clamp(m_pitch + pitchDelta, k_PitchMin, k_PitchMax);
            const Vector3 eyeAfter = ComputeCameraPosition();
            m_center.x += eyeBefore.x - eyeAfter.x;
            m_center.y += eyeBefore.y - eyeAfter.y;
            m_center.z += eyeBefore.z - eyeAfter.z;
        }

        void EditorCamera::ApplyFlyMove(
            float forwardAxis, float strafeAxis, float verticalAxis, float dt, float speedScale) noexcept
        {
            if (forwardAxis == 0.0f && strafeAxis == 0.0f && verticalAxis == 0.0f)
            {
                return;
            }

            const float cosPitch = std::cos(m_pitch);
            const float sinPitch = std::sin(m_pitch);
            const float sinYaw = std::sin(m_yaw);
            const float cosYaw = std::cos(m_yaw);
            // 視点から注視点への単位方向を使い、上下の傾きを含めて進める
            const Vector3 forward{-cosPitch * sinYaw, -sinPitch, -cosPitch * cosYaw};
            // 画面右は世界の上と forward の外積。左手系なので yaw=0 では -X
            const Vector3 right{-cosYaw, 0.0f, sinYaw};
            // 近距離でも操作できる速さを残すため、距離比例の下限を設ける
            const float step = m_tuning.keyMoveSpeed * std::max(m_distance, k_MinMoveDistance) * dt * speedScale;
            m_center.x += (forward.x * forwardAxis + right.x * strafeAxis) * step;
            m_center.y += (forward.y * forwardAxis + verticalAxis) * step;
            m_center.z += (forward.z * forwardAxis + right.z * strafeAxis) * step;
        }

        bool EditorCamera::HasValidTuning() const noexcept
        {
            return IsPositiveFinite(m_tuning.springOmega) && IsPositiveFinite(m_tuning.mouseSensOrbit) &&
                   IsPositiveFinite(m_tuning.mouseSensPan) && IsPositiveFinite(m_tuning.mouseSensZoom) &&
                   IsPositiveFinite(m_tuning.padSensOrbit) && IsPositiveFinite(m_tuning.padSensPan) &&
                   IsPositiveFinite(m_tuning.padSensZoom) && IsPositiveFinite(m_tuning.keyMoveSpeed) &&
                   IsPositiveFinite(m_tuning.boostMoveScale);
        }

        void EditorCamera::ApplyInput(const EditorCameraInput& input) noexcept
        {
            if (!HasValidTuning())
            {
                return;
            }
            if (input.flying)
            {
                ApplyLook(input.lookYawPixels * m_tuning.mouseSensOrbit,
                          input.lookPitchPixels * m_tuning.mouseSensOrbit);
            }
            if (input.panXPixels != 0.0f || input.panYPixels != 0.0f)
            {
                ApplyPan(input.panXPixels * m_tuning.mouseSensPan, input.panYPixels * m_tuning.mouseSensPan);
            }
            ApplyZoom(input.wheelNotches * m_tuning.mouseSensZoom);
            if (input.flying)
            {
                float speedScale = 1.0f;
                if (input.boost)
                {
                    speedScale = m_tuning.boostMoveScale;
                }
                ApplyFlyMove(input.forwardAxis, input.strafeAxis, input.verticalAxis, input.deltaSeconds, speedScale);
            }

            m_distance += (m_desiredDistance - m_distance) * std::min(1.0f, m_tuning.springOmega * input.deltaSeconds);
        }

        Vector3 EditorCamera::ComputeCameraPosition() const noexcept
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
            // パッドの入力は ApplyInput より前に設定値を使うため、ここでも断る
            if (!HasValidTuning())
            {
                return;
            }
            const float dt = OS::FrameTimer::FixedDelta();
            OS::Input& input = OS::Input::Get();

            // Platform Input から 1 フレーム分の自由視点入力を組む。感度適用とバネは注入する側に任せる
            EditorCameraInput frameInput{};
            frameInput.deltaSeconds = dt;

            // マウス入力: 右ドラッグで見回し、中ドラッグで pan、ホイールで zoom
            const bool wantMouse = input.UiWantsMouse();
            if (!wantMouse)
            {
                OS::Mouse& mouse = input.Mouse();
                // 右ドラッグ中はその場で見回すフライ視点。eye 固定で回し、WASD/QE の移動も許可する
                frameInput.flying = mouse.IsHeld(OS::MouseButton::Right);
                if (frameInput.flying)
                {
                    frameInput.lookYawPixels = static_cast<float>(mouse.GetDeltaX());
                    frameInput.lookPitchPixels = static_cast<float>(mouse.GetDeltaY());
                }
                if (mouse.IsHeld(OS::MouseButton::Middle))
                {
                    frameInput.panXPixels = static_cast<float>(mouse.GetDeltaX());
                    frameInput.panYPixels = static_cast<float>(mouse.GetDeltaY());
                }
                // GetWheelDelta は WHEEL_DELTA=120 単位なので /120 で 1 刻み=1.0 に正規化
                frameInput.wheelNotches = static_cast<float>(mouse.GetWheelDelta()) / 120.0f;
            }

            // 右ドラッグ中のみ WASD で視線方向へフライ、Q E で world 上下する。UI がキー入力中なら無視する
            if (frameInput.flying && !input.UiWantsKeyboard())
            {
                OS::Keyboard& kb = input.Keyboard();
                if (kb.IsHeld(OS::Key::W))
                {
                    frameInput.forwardAxis += 1.0f;
                }
                if (kb.IsHeld(OS::Key::S))
                {
                    frameInput.forwardAxis -= 1.0f;
                }
                if (kb.IsHeld(OS::Key::D))
                {
                    frameInput.strafeAxis += 1.0f;
                }
                if (kb.IsHeld(OS::Key::A))
                {
                    frameInput.strafeAxis -= 1.0f;
                }
                if (kb.IsHeld(OS::Key::E))
                {
                    frameInput.verticalAxis += 1.0f;
                }
                if (kb.IsHeld(OS::Key::Q))
                {
                    frameInput.verticalAxis -= 1.0f;
                }
                if (kb.IsHeld(OS::Key::Shift))
                {
                    frameInput.boost = true;
                }
            }

            // Gamepad は ImGui キャプチャ対象外、常に入力する。自由視点入力とは別枠でその場に適用する
            {
                OS::Gamepad& gp = input.Gamepad();
                if (gp.IsConnected())
                {
                    const OS::Stick rs = gp.RightStick();
                    const OS::Stick ls = gp.LeftStick();
                    ApplyOrbit(rs.x * m_tuning.padSensOrbit * dt, -rs.y * m_tuning.padSensOrbit * dt);
                    ApplyPan(ls.x * m_tuning.padSensPan * dt, -ls.y * m_tuning.padSensPan * dt);
                    ApplyZoom((gp.RightTrigger() - gp.LeftTrigger()) * m_tuning.padSensZoom * dt);
                }
            }

            ApplyInput(frameInput);
        }

        CameraPose EditorCamera::Pose() const noexcept
        {
            CameraPose pose{};
            pose.position = ComputeCameraPosition();
            pose.target = m_center;
            pose.up = Vector3{0.0f, 1.0f, 0.0f};
            pose.fovY = m_fovY;
            pose.nearPlane = m_nearPlane;
            pose.farPlane = m_farPlane;
            return pose;
        }
} // namespace NS::Editor
