#include "Runtime/Object/Components/PlayerInputComponent.h"

#include "Runtime/Object/CameraSubsystem.h"
#include "Runtime/Object/Components/CameraBrainComponent.h"
#include "Runtime/Object/Components/CharacterMovementComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/Gamepad.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Keyboard.h"

#include <cmath>

namespace
{
    /// 水平 forward を XZ 平面正規化。長さ 0 の入力は world +Z にフォールバック
    [[nodiscard]] NS::Math::Vector3 NormalizeHorizontal(const NS::Math::Vector3& v) noexcept
    {
        const float lenSq = v.x * v.x + v.z * v.z;
        if (lenSq < 1e-8f)
            return NS::Math::Vector3{0.0f, 0.0f, 1.0f};
        const float invLen = 1.0f / std::sqrt(lenSq);
        return NS::Math::Vector3{v.x * invLen, 0.0f, v.z * invLen};
    }
} // namespace

namespace NS::Object
{
    PlayerInputComponent::PlayerInputComponent() noexcept
        : Component(NS::Object::TickPriority::EarlyUpdate)
    {}

    void PlayerInputComponent::SetCameraForward(const NS::Math::Vector3& cameraForwardHorizontal) noexcept
    {
        m_cameraForward = NormalizeHorizontal(cameraForwardHorizontal);
    }

    void PlayerInputComponent::OnStart()
    {
        if (Owner() != nullptr)
            m_movement = Owner()->FindComponent<CharacterMovementComponent>();
        else
            m_movement = nullptr;
    }

    void PlayerInputComponent::OnUpdate()
    {
        if (!IsActive() || m_movement == nullptr)
            return;

        // camera 相対移動の基準 forward は Brain から自分で読む。 Brain 不在 (テスト等) は注入値のまま
        if (Owner() != nullptr && Owner()->OwningScene() != nullptr)
        {
            if (auto* cameras = Owner()->OwningScene()->GetSubsystem<CameraSubsystem>())
            {
                if (CameraBrainComponent* brain = cameras->Brain())
                    m_cameraForward = NormalizeHorizontal(brain->ForwardHorizontal());
            }
        }

        auto& input = NS::Platform::Input::Get();
        const auto& kb = input.Keyboard();
        const auto& pad = input.Gamepad(0);

        // UI のテキスト入力中はキーボード由来の移動 / ジャンプを取り合わない。 gamepad は維持する
        const bool wantKb = input.UiWantsKeyboard();

        // WASD の前後左右入力
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

        // スティック合成と入力の大きさクランプ
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

        // camera 相対の world 方向変換
        const NS::Math::Vector3 fwd = NormalizeHorizontal(m_cameraForward);
        const NS::Math::Vector3 right{fwd.z, 0.0f, -fwd.x};

        const NS::Math::Vector3 worldDir{
            right.x * localX + fwd.x * localZ,
            0.0f,
            right.z * localX + fwd.z * localZ,
        };

        m_movement->SetDesiredMove(worldDir, speedScale);
        // climb 中は camera 回転をかける前の生ローカル入力を渡す。 前で登り、 右で面に沿って右へ動く
        m_movement->SetClimbMove(localX, localZ);

        // ジャンプの押下と長押し
        const bool jumpPressed =
            (!wantKb && kb.IsPressed(NS::Platform::Key::Space)) || pad.IsPressed(NS::Platform::GamepadButton::A);
        const bool jumpHeld =
            (!wantKb && kb.IsHeld(NS::Platform::Key::Space)) || pad.IsHeld(NS::Platform::GamepadButton::A);

        if (jumpPressed)
            m_movement->SetJumpPressed();
        m_movement->SetJumpHeld(jumpHeld);
    }

    NS_CLASS(PlayerInputComponent)
} // namespace NS::Object
