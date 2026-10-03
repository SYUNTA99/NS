#include "Runtime/Object/Components/PlayerInput.h"

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/Gamepad.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Keyboard.h"
#include "Runtime/Platform/Mouse.h"

#include <cmath>

namespace
{
    //! 水平 forward を XZ 平面正規化。長さ 0 の入力は world +Z にフォールバック
    [[nodiscard]] NS::Core::Vector3 NormalizeHorizontal(const NS::Core::Vector3& v) noexcept
    {
        NS::Core::Vector3 out{};
        if (!NS::Core::TryNormalizeHorizontal(v, out))
        {
            return NS::Core::Vector3{0.0f, 0.0f, 1.0f};
        }
        return out;
    }
} // namespace

namespace NS::Obj
{
    PlayerInput::PlayerInput() noexcept : Component() {}

    void PlayerInput::SetCameraForward(const NS::Core::Vector3& cameraForwardHorizontal) noexcept
    {
        m_cameraForward = NormalizeHorizontal(cameraForwardHorizontal);
    }

    void PlayerInput::SetDesiredMove(const NS::Core::Vector3& worldDir, float speedScale01) noexcept
    {
        m_desiredDir = worldDir;
        m_desiredSpeedScale = NS::Core::Clamp(speedScale01, 0.0f, 1.0f);
    }

    void PlayerInput::SetClimbMove(float localRight, float localForward) noexcept
    {
        m_climbRight = NS::Core::Clamp(localRight, -1.0f, 1.0f);
        m_climbForward = NS::Core::Clamp(localForward, -1.0f, 1.0f);
    }

    void PlayerInput::ConsumePressed() noexcept
    {
        m_jumpPressed = false;
        m_releaseLedgePressed = false;
    }

    void PlayerInput::ResetMovementInput() noexcept
    {
        m_desiredDir = NS::Core::Vector3{};
        m_desiredSpeedScale = 0.0f;
        m_climbRight = 0.0f;
        m_climbForward = 0.0f;
        m_jumpHeld = false;
        ConsumePressed();
    }

    void PlayerInput::SetLocked(bool locked) noexcept
    {
        m_locked = locked;
        if (locked)
        {
            // 部品が回っていない間に止めても、止めた瞬間の値を残さない
            ResetMovementInput();
            m_slamHeld = false;
        }
    }

    void PlayerInput::OnUpdate()
    {
        if (!IsActive())
        {
            return;
        }

        // camera 相対移動の基準 forward は CameraManager から自分で読む。CameraManager 不在 (テスト等) は注入値のまま
        if (Owner() != nullptr && Owner()->GetCameraManager() != nullptr)
        {
            m_cameraForward = NormalizeHorizontal(CameraForwardHorizontal(*Owner()));
        }

        NS::Platform::Input& input = NS::Platform::Input::Get();
        const NS::Platform::Keyboard& kb = input.Keyboard();
        const NS::Platform::Gamepad& pad = input.Gamepad(0);

        // トリガーは XInput の既定のしきい値を NormalizeTrigger が先に切っているので、0 を超えたかだけ見る
        const bool releaseLedgeHeld = pad.LeftTrigger() > 0.0f;
        if (m_locked)
        {
            // 止めの間も手放しのトリガーの控えは進める。引いたまま止めが明けた歩を押した瞬間と読まない
            m_prevReleaseLedgeHeld = releaseLedgeHeld;
            ResetMovementInput();
            m_slamHeld = false;
            return;
        }

        // UI のテキスト入力中はキーボード由来の移動 / ジャンプを取り合わない。gamepad は維持する
        const bool wantKb = input.UiWantsKeyboard();

        // WASD の前後左右入力
        float kbForward = 0.0f;
        float kbRight = 0.0f;
        if (!wantKb)
        {
            if (kb.IsHeld(NS::Platform::Key::W))
            {
                kbForward += 1.0f;
            }
            if (kb.IsHeld(NS::Platform::Key::S))
            {
                kbForward -= 1.0f;
            }
            if (kb.IsHeld(NS::Platform::Key::A))
            {
                kbRight -= 1.0f;
            }
            if (kb.IsHeld(NS::Platform::Key::D))
            {
                kbRight += 1.0f;
            }
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
        const NS::Core::Vector3 fwd = NormalizeHorizontal(m_cameraForward);
        const NS::Core::Vector3 right{fwd.z, 0.0f, -fwd.x};

        const NS::Core::Vector3 worldDir{
            right.x * localX + fwd.x * localZ,
            0.0f,
            right.z * localX + fwd.z * localZ,
        };

        // ジャンプの押下と長押し
        const bool jumpPressed =
            (!wantKb && kb.IsPressed(NS::Platform::Key::Space)) || pad.IsPressed(NS::Platform::GamepadButton::A);
        const bool jumpHeld =
            (!wantKb && kb.IsHeld(NS::Platform::Key::Space)) || pad.IsHeld(NS::Platform::GamepadButton::A);

        m_desiredDir = worldDir;
        m_desiredSpeedScale = speedScale;
        m_climbRight = localX;
        m_climbForward = localZ;
        m_jumpPressed = m_jumpPressed || jumpPressed;
        m_jumpHeld = jumpHeld;

        // 体当たりのマウス左は、ゲームがマウスのボタンを受け取っている間だけ数える
        m_slamHeld = (input.GameReceivesMouseButton(NS::Platform::MouseButton::Left) &&
                      input.Mouse().IsHeld(NS::Platform::MouseButton::Left)) ||
                     pad.IsHeld(NS::Platform::GamepadButton::X);

        // 手放しは専用のマウス右ボタン / 左トリガー。後ろ入力と兼ねると、カメラ側の縁へ寄せた入力で手を放す
        const bool mouseFree = !input.UiWantsMouse();
        m_releaseLedgePressed = m_releaseLedgePressed ||
                                (mouseFree && input.Mouse().IsPressed(NS::Platform::MouseButton::Right)) ||
                                (releaseLedgeHeld && !m_prevReleaseLedgeHeld);
        m_prevReleaseLedgeHeld = releaseLedgeHeld;
    }

    NS_CLASS(PlayerInput)
} // namespace NS::Obj
