#include "Framework/Scene/PlayerInputComponent.h"

#include "Framework/Platform/Gamepad.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Scene/CharacterMovementComponent.h"
#include "Framework/Scene/GameObject.h"

#include <cmath>

namespace
{
    /// 水平 forward を XZ 平面正規化。長さ 0 の入力は world -Z+ にフォールバック。
    [[nodiscard]] NS::Core::Vector3 NormalizeHorizontal(const NS::Core::Vector3& v) noexcept
    {
        const float lenSq = v.x * v.x + v.z * v.z;
        if (lenSq < 1e-8f)
            return NS::Core::Vector3{0.0f, 0.0f, 1.0f};
        const float invLen = 1.0f / std::sqrt(lenSq);
        return NS::Core::Vector3{v.x * invLen, 0.0f, v.z * invLen};
    }
} // namespace

namespace NS::Scene
{
    PlayerInputComponent::PlayerInputComponent(CharacterMovementComponent* movement) noexcept : m_movement(movement) {}

    PlayerInputComponent::PlayerInputComponent(NS::Scene::GameObject* owner,
                                               CharacterMovementComponent* movement) noexcept
        : Component(owner, static_cast<int>(NS::Scene::TickPriority::Input)), m_movement(movement)
    {}

    void PlayerInputComponent::SetCameraForward(const NS::Core::Vector3& cameraForwardHorizontal) noexcept
    {
        m_cameraForward = NormalizeHorizontal(cameraForwardHorizontal);
    }

    void PlayerInputComponent::SetInput(NS::Platform::Input* input) noexcept
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
        if (kb.IsHeld(NS::Platform::Key::W))
            kbForward += 1.0f;
        if (kb.IsHeld(NS::Platform::Key::S))
            kbForward -= 1.0f;
        if (kb.IsHeld(NS::Platform::Key::A))
            kbRight -= 1.0f;
        if (kb.IsHeld(NS::Platform::Key::D))
            kbRight += 1.0f;

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

        const NS::Core::Vector3 fwd = NormalizeHorizontal(m_cameraForward);
        const NS::Core::Vector3 right{fwd.z, 0.0f, -fwd.x};

        const NS::Core::Vector3 worldDir{
            right.x * localX + fwd.x * localZ,
            0.0f,
            right.z * localX + fwd.z * localZ,
        };

        m_movement->SetDesiredMove(worldDir, speedScale);

        const bool jumpPressed =
            kb.IsPressed(NS::Platform::Key::Space) || pad.IsPressed(NS::Platform::GamepadButton::A);
        const bool jumpHeld = kb.IsHeld(NS::Platform::Key::Space) || pad.IsHeld(NS::Platform::GamepadButton::A);

        if (jumpPressed)
            m_movement->SetJumpPressed();
        m_movement->SetJumpHeld(jumpHeld);
    }
} // namespace NS::Scene
