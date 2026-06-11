#include "Framework/Scene/Components/PlayerInputComponent.h"

#include "Framework/Platform/Gamepad.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Scene/Components/CharacterMovementComponent.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/UI/ImGuiContext.h"

#include <cmath>

namespace
{
    /// 水平 forward を XZ 平面正規化。長さ 0 の入力は world -Z+ にフォールバック
    [[nodiscard]] NS::Math::Vector3 NormalizeHorizontal(const NS::Math::Vector3& v) noexcept
    {
        const float lenSq = v.x * v.x + v.z * v.z;
        if (lenSq < 1e-8f)
            return NS::Math::Vector3{0.0f, 0.0f, 1.0f};
        const float invLen = 1.0f / std::sqrt(lenSq);
        return NS::Math::Vector3{v.x * invLen, 0.0f, v.z * invLen};
    }
} // namespace

namespace NS::Scene
{
    PlayerInputComponent::PlayerInputComponent(CharacterMovementComponent* movement) noexcept
        : Component(static_cast<int>(NS::Scene::TickPriority::Input)), m_movement(movement)
    {}

    void PlayerInputComponent::SetCameraForward(const NS::Math::Vector3& cameraForwardHorizontal) noexcept
    {
        m_cameraForward = NormalizeHorizontal(cameraForwardHorizontal);
    }

    void PlayerInputComponent::SetInput(NS::Platform::Input* input) noexcept
    {
        m_input = input;
    }

    void PlayerInputComponent::SetImGui(NS::UI::ImGuiContext* imgui) noexcept
    {
        m_imgui = imgui;
    }

    void PlayerInputComponent::OnUpdate()
    {
        if (!IsActive() || m_movement == nullptr || m_input == nullptr)
            return;

        const auto& kb = m_input->Keyboard();
        const auto& pad = m_input->Gamepad(0);

        // ImGui のテキスト入力中はキーボード由来の移動 / ジャンプを取り合わない (gamepad は維持)
        const bool wantKb = (m_imgui != nullptr) && m_imgui->WantCaptureKeyboard();

        float kbForward = 0.0f;
        float kbRight = 0.0f;
        if (!wantKb)
        {
            if (kb.IsHeld(NS::Platform::Key::W))
                kbForward += 1.0f;
            if (kb.IsHeld(NS::Platform::Key::S))
                kbForward -= 1.0f;
            if (kb.IsHeld(NS::Platform::Key::A))
                kbRight -= 1.0f;
            if (kb.IsHeld(NS::Platform::Key::D))
                kbRight += 1.0f;
        }

        const NS::Platform::Stick lstick = pad.LeftStick();

        float localX = kbRight + lstick.x;
        float localZ = kbForward + lstick.y;

        const float localMag = std::sqrt(localX * localX + localZ * localZ);
        float speedScale = 0.0f;
        if (localMag > 1.0f)
        {
            localX /= localMag;
            localZ /= localMag;
            speedScale = 1.0f;
        }
        else
        {
            speedScale = localMag;
        }

        const NS::Math::Vector3 fwd = NormalizeHorizontal(m_cameraForward);
        const NS::Math::Vector3 right{fwd.z, 0.0f, -fwd.x};

        const NS::Math::Vector3 worldDir{
            right.x * localX + fwd.x * localZ,
            0.0f,
            right.z * localX + fwd.z * localZ,
        };

        m_movement->SetDesiredMove(worldDir, speedScale);
        // climb 中は camera 回転をかける前の生ローカル入力を渡す (前=登る、 右=面に沿って右)
        m_movement->SetClimbMove(localX, localZ);

        const bool jumpPressed =
            (!wantKb && kb.IsPressed(NS::Platform::Key::Space)) || pad.IsPressed(NS::Platform::GamepadButton::A);
        const bool jumpHeld =
            (!wantKb && kb.IsHeld(NS::Platform::Key::Space)) || pad.IsHeld(NS::Platform::GamepadButton::A);

        if (jumpPressed)
            m_movement->SetJumpPressed();
        m_movement->SetJumpHeld(jumpHeld);
    }
} // namespace NS::Scene
