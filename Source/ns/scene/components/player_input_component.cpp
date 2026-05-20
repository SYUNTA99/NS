#include "ns/scene/components/player_input_component.h"

#include "ns/platform/gamepad.h"
#include "ns/platform/input.h"
#include "ns/platform/keyboard.h"
#include "ns/scene/components/character_movement_component.h"

#include <cmath>

namespace
{
    /// 水平 forward を XZ 平面正規化。長さ 0 の入力は world -Z+ にフォールバック。
    [[nodiscard]] ns::core::Vector3 NormalizeHorizontal(const ns::core::Vector3& v) noexcept
    {
        const float lenSq = v.x * v.x + v.z * v.z;
        if (lenSq < 1e-8f)
            return ns::core::Vector3{0.0f, 0.0f, 1.0f};
        const float invLen = 1.0f / std::sqrt(lenSq);
        return ns::core::Vector3{v.x * invLen, 0.0f, v.z * invLen};
    }
} // namespace

namespace ns::scene
{
    PlayerInputComponent::PlayerInputComponent(CharacterMovementComponent* movement) noexcept : m_movement(movement) {}

    void PlayerInputComponent::SetCameraForward(const ns::core::Vector3& cameraForwardHorizontal) noexcept
    {
        m_cameraForward = NormalizeHorizontal(cameraForwardHorizontal);
    }

    void PlayerInputComponent::SetInput(ns::platform::Input* input) noexcept
    {
        m_input = input;
    }

    void PlayerInputComponent::OnUpdate(float dt)
    {
        (void)dt;
        if (!IsActive() || m_movement == nullptr || m_input == nullptr)
            return;

        const auto& kb = m_input->Keyboard();
        const auto& pad = m_input->Gamepad(0);

        float kbForward = 0.0f;
        float kbRight = 0.0f;
        if (kb.IsHeld(ns::platform::Key::W))
            kbForward += 1.0f;
        if (kb.IsHeld(ns::platform::Key::S))
            kbForward -= 1.0f;
        if (kb.IsHeld(ns::platform::Key::A))
            kbRight -= 1.0f;
        if (kb.IsHeld(ns::platform::Key::D))
            kbRight += 1.0f;

        const ns::platform::Stick lstick = pad.LeftStick();

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

        const ns::core::Vector3 fwd = NormalizeHorizontal(m_cameraForward);
        const ns::core::Vector3 right{fwd.z, 0.0f, -fwd.x};

        const ns::core::Vector3 worldDir{
            right.x * localX + fwd.x * localZ,
            0.0f,
            right.z * localX + fwd.z * localZ,
        };

        m_movement->SetDesiredMove(worldDir, speedScale);

        const bool jumpPressed =
            kb.IsPressed(ns::platform::Key::Space) || pad.IsPressed(ns::platform::GamepadButton::A);
        const bool jumpHeld = kb.IsHeld(ns::platform::Key::Space) || pad.IsHeld(ns::platform::GamepadButton::A);

        if (jumpPressed)
            m_movement->SetJumpPressed();
        m_movement->SetJumpHeld(jumpHeld);
    }
} // namespace ns::scene
