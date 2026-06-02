#include "Framework/Scene/EditorCameraComponent.h"

#include "Framework/Core/Clock.h"
#include "Framework/Platform/Input.h"
#include "Framework/Scene/CameraComponent.h"
#include "Framework/UI/ImGuiContext.h"

#include <algorithm>
#include <cmath>

namespace NS::Scene
{

    EditorCameraComponent::EditorCameraComponent(GameObject* owner) noexcept
        : Component(owner, static_cast<int>(TickPriority::Camera))
    {}

    void EditorCameraComponent::SetCamera(CameraComponent* camera) noexcept
    {
        m_camera = camera;
    }

    void EditorCameraComponent::SetInput(NS::Platform::Input* input) noexcept
    {
        m_input = input;
    }

    void EditorCameraComponent::SetImGui(NS::UI::ImGuiContext* imgui) noexcept
    {
        m_imgui = imgui;
    }

    void EditorCameraComponent::SetYawPitch(float yaw, float pitch) noexcept
    {
        m_yaw = yaw;
        m_pitch = std::clamp(pitch, kPitchMin, kPitchMax);
    }

    void EditorCameraComponent::SetCenter(NS::Core::Vector3 center) noexcept
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
        const NS::Core::Vector3 right{cosYaw, 0.0f, -sinYaw};
        const NS::Core::Vector3 up{0.0f, 1.0f, 0.0f};
        const float scale = m_distance * 0.1f;
        m_center.x += (right.x * panX + up.x * panY) * scale;
        m_center.y += (right.y * panX + up.y * panY) * scale;
        m_center.z += (right.z * panX + up.z * panY) * scale;
    }

    void EditorCameraComponent::ApplyZoom(float zoomDelta) noexcept
    {
        // 加算式だと近距離で 1 notch が画面の半分を動き、 遠距離では微動にしかならず
        // 体感の zoom が非対称になる。 距離 N に対して一定比率で動かす log scale で
        // 対称化する。 zoomDelta = 1 で ×0.9 (近づく)、 -1 で ÷0.9 (離れる)
        if (zoomDelta == 0.0f)
            return;
        const float factor = std::pow(0.9f, zoomDelta);
        m_desiredDistance = std::clamp(m_desiredDistance * factor, kMinDistance, kMaxDistance);
    }

    NS::Core::Vector3 EditorCameraComponent::ComputeCameraPosition() const noexcept
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

        // Mouse 入力。 ImGui がフォーカス中なら無視する
        const bool wantMouse = (m_imgui != nullptr) && m_imgui->WantCaptureMouse();
        if (m_input != nullptr && !wantMouse)
        {
            auto& mouse = m_input->Mouse();
            if (mouse.IsHeld(NS::Platform::MouseButton::Right))
            {
                ApplyOrbit(static_cast<float>(mouse.DeltaX()) * m_mouseSensOrbit,
                           static_cast<float>(mouse.DeltaY()) * m_mouseSensOrbit);
            }
            if (mouse.IsHeld(NS::Platform::MouseButton::Middle))
            {
                ApplyPan(static_cast<float>(mouse.DeltaX()) * m_mouseSensPan,
                         static_cast<float>(mouse.DeltaY()) * m_mouseSensPan);
            }
            // Wheel: 1 notch (= WHEEL_DELTA 120 単位) を 1 zoomDelta に正規化
            // ApplyZoom が log scale なので 1 notch = 10% × m_mouseSensZoom の距離変化
            ApplyZoom(static_cast<float>(mouse.WheelDelta()) / 120.0f * m_mouseSensZoom);
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

        if (m_camera != nullptr)
        {
            m_camera->SetPosition(ComputeCameraPosition());
            m_camera->SetTarget(m_center);
        }
    }

    namespace EditorGridMath
    {

        NS::Core::Ray ScreenToWorldRay(const NS::Core::Matrix& viewProjection,
                                       NS::Core::Size2D viewport,
                                       int mouseX,
                                       int mouseY) noexcept
        {
            const float ndcX = (2.0f * static_cast<float>(mouseX)) / static_cast<float>(viewport.width) - 1.0f;
            const float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / static_cast<float>(viewport.height);

            NS::Core::Matrix inv = viewProjection.Invert();

            const NS::Core::Vector4 nearH{ndcX, ndcY, 0.0f, 1.0f};
            const NS::Core::Vector4 farH{ndcX, ndcY, 1.0f, 1.0f};

            const NS::Core::Vector4 wNearH = NS::Core::Vector4::Transform(nearH, inv);
            const NS::Core::Vector4 wFarH = NS::Core::Vector4::Transform(farH, inv);

            const float invWNear = (std::abs(wNearH.w) > 1e-6f) ? (1.0f / wNearH.w) : 0.0f;
            const float invWFar = (std::abs(wFarH.w) > 1e-6f) ? (1.0f / wFarH.w) : 0.0f;

            NS::Core::Vector3 wNear{wNearH.x * invWNear, wNearH.y * invWNear, wNearH.z * invWNear};
            NS::Core::Vector3 wFar{wFarH.x * invWFar, wFarH.y * invWFar, wFarH.z * invWFar};
            NS::Core::Vector3 dir = wFar - wNear;
            dir.Normalize();
            return NS::Core::Ray{wNear, dir};
        }

        NS::Core::Vector3 SnapWorldPointToGrid(NS::Core::Vector3 p, float g) noexcept
        {
            // 最近接 cell center に snap (0.5 を足してから floor で四捨五入相当)
            const float gx = std::floor(p.x / g + 0.5f) * g;
            const float gy = std::floor(p.y / g + 0.5f) * g;
            const float gz = std::floor(p.z / g + 0.5f) * g;
            return {gx, gy, gz};
        }

        NS::Core::Vector3 SnapHitToPlacementCell(NS::Core::Vector3 hit, NS::Core::Vector3 normal, float g) noexcept
        {
            const NS::Core::Vector3 base = SnapWorldPointToGrid(hit, g);
            return {base.x + normal.x * g, base.y + normal.y * g, base.z + normal.z * g};
        }

        bool TryGroundPlaneFallback(const NS::Core::Ray& ray, NS::Core::Vector3& outCenter, float g) noexcept
        {
            // 上向き ray / 水平 ray は地面に当たらない
            if (ray.direction.y > -1e-4f)
                return false;
            const float t = -ray.position.y / ray.direction.y;
            if (t < 0.0f)
                return false;
            const NS::Core::Vector3 hit{
                ray.position.x + ray.direction.x * t,
                0.0f,
                ray.position.z + ray.direction.z * t,
            };
            outCenter = SnapWorldPointToGrid(hit, g);
            outCenter.y = 0.0f;
            return true;
        }

        NS::Core::Quaternion RotationToQuaternion(std::uint8_t rotation) noexcept
        {
            const std::uint8_t r = static_cast<std::uint8_t>(rotation & 0x03);
            constexpr float kQuarter = 1.5707963267948966f;
            const float angle = static_cast<float>(r) * kQuarter;
            return NS::Core::Quaternion::CreateFromAxisAngle({0.0f, 1.0f, 0.0f}, angle);
        }

    } // namespace EditorGridMath

} // namespace NS::Scene
