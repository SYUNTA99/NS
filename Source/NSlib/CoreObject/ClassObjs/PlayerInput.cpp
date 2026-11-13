#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/Core/Windows/Gamepad.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Core/Windows/InputReader.h"
#include "NSlib/Core/Windows/Keyboard.h"
#include "NSlib/Core/Windows/Mouse.h"

#include <cmath>

namespace
{
    // 水平 forward を XZ 平面正規化。長さ 0 の入力は世界座標の +Z にフォールバック
    [[nodiscard]] NS::Vector3 NormalizeHorizontal(const NS::Vector3& v) noexcept
    {
        NS::Vector3 out{};
        if (!NS::TryNormalizeHorizontal(v, out))
        {
            return NS::Vector3{0.0f, 0.0f, 1.0f};
        }
        return out;
    }

    // 行は順に水平の右・真上・水平の前。右は前を真上の周りに 90 度回した向き
    [[nodiscard]] NS::Matrix MakeInputMatrix(const NS::Vector3& cameraForwardHorizontal) noexcept
    {
        const NS::Vector3 forward = NormalizeHorizontal(cameraForwardHorizontal);
        const NS::Vector3 right{forward.z, 0.0f, -forward.x};
        return NS::Matrix{right, NS::Vector3{0.0f, 1.0f, 0.0f}, forward};
    }
} // namespace

namespace NS
{
    void PlayerInput::SetCameraForward(const Vector3& cameraForwardHorizontal) noexcept
    {
        m_inputMatrix = MakeInputMatrix(cameraForwardHorizontal);
    }

    void PlayerInput::SetStick(float right, float forward) noexcept
    {
        float speedScale = std::sqrt(right * right + forward * forward);
        if (speedScale > 1.0f)
        {
            right /= speedScale;
            forward /= speedScale;
            speedScale = 1.0f;
        }
        m_stickRight = right;
        m_stickForward = forward;
        m_desiredDir = Vector3::TransformNormal(Vector3{right, 0.0f, forward}, m_inputMatrix);
        m_desiredSpeedScale = speedScale;
    }

    void PlayerInput::ConsumePressed() noexcept
    {
        m_jumpPressed = false;
        m_releaseLedgePressed = false;
        m_slamSeen = false;
        m_respawnPressed = false;
    }

    void PlayerInput::ResetMovementInput() noexcept
    {
        m_desiredDir = Vector3{};
        m_desiredSpeedScale = 0.0f;
        m_stickRight = 0.0f;
        m_stickForward = 0.0f;
        m_jumpHeld = false;
        ConsumePressed();
        // やり直しと止めへ、読んでいない間 (消えている間・編集の間) の押しを持ち越さない
        DiscardUnreadPresses();
    }

    void PlayerInput::SetLocked(bool locked) noexcept
    {
        m_locked = locked;
        if (locked)
        {
            // 部品が回っていない間に止めても、止めた瞬間の値を残さない
            ResetMovementInput();
            m_slamHeld = false;
            return;
        }
        // 入力を止めている間に Update が回らなかった時も、その間の押しを解いた後へ持ち越さない
        DiscardUnreadPresses();
    }

    void PlayerInput::Update(const IGetCamera* camera)
    {
        NS_ASSERT(Scene, camera != nullptr, "PlayerInput::Update: カメラを引くアクターが空");

        // camera 相対移動の基準 forward は CameraManager から読む。CameraManager 不在 (テスト等) は注入値のまま
        if (camera->GetCameraManager() != nullptr)
        {
            m_inputMatrix = MakeInputMatrix(CalcCameraForwardHorizontal(camera));
        }

        OS::Input& input = OS::Input::Get();
        const OS::Keyboard& kb = input.Keyboard();
        const OS::Gamepad& pad = input.Gamepad();

        // 止めの間も読む。止めの間の押しを、止めが明けた歩で押したと読まない
        const OS::InputPresses presses = m_reader.Read();
        if (m_locked)
        {
            ResetMovementInput();
            m_slamHeld = false;
            return;
        }

        // UI のテキスト入力中はキーボード由来の移動 / ジャンプを取り合わない。gamepad は維持する
        const bool wantKb = input.UiWantsKeyboard();

        float kbForward = 0.0f;
        float kbRight = 0.0f;
        if (!wantKb)
        {
            if (kb.IsHeld(OS::Key::W))
            {
                kbForward += 1.0f;
            }
            if (kb.IsHeld(OS::Key::S))
            {
                kbForward -= 1.0f;
            }
            if (kb.IsHeld(OS::Key::A))
            {
                kbRight -= 1.0f;
            }
            if (kb.IsHeld(OS::Key::D))
            {
                kbRight += 1.0f;
            }
        }

        const OS::Stick lstick = pad.LeftStick();

        const bool jumpPressed =
            (!wantKb && presses.WasPressed(OS::Key::Space)) || presses.WasPressed(OS::GamepadButton::A);
        const bool jumpHeld = (!wantKb && kb.IsHeld(OS::Key::Space)) || pad.IsHeld(OS::GamepadButton::A);

        SetStick(kbRight + lstick.x, kbForward + lstick.y);
        m_jumpPressed = m_jumpPressed || jumpPressed;
        m_jumpHeld = jumpHeld;
        m_respawnPressed =
            m_respawnPressed || (!wantKb && presses.WasPressed(OS::Key::R)) || presses.WasPressed(OS::GamepadButton::Back);

        // 突進のマウス左は、ゲームがマウスのボタンを受け取っている間だけ数える
        const bool mouseLeftToGame = input.GameReceivesMouseButton(OS::MouseButton::Left);
        const bool slamHeld =
            (mouseLeftToGame && input.Mouse().IsHeld(OS::MouseButton::Left)) || pad.IsHeld(OS::GamepadButton::X);
        // 前に読んだ後に押して放した分も押したと見せる。読む間に押して放した突進を落とさない
        const bool slamPressed = (mouseLeftToGame && presses.WasPressed(OS::MouseButton::Left)) ||
                                 presses.WasPressed(OS::GamepadButton::X);
        // 止めの間は状態の段が下ろさないので、ConsumePressed まで押したままに見せる
        m_slamHeld = slamHeld || slamPressed || m_slamSeen;
        m_slamSeen = m_slamHeld;

        // 手放しは専用のマウス右ボタン / 左トリガー。後ろ入力と兼ねると、カメラ側の縁へ寄せた入力で手を放す
        const bool mouseFree = !input.UiWantsMouse();
        m_releaseLedgePressed = m_releaseLedgePressed ||
                                (mouseFree && presses.WasPressed(OS::MouseButton::Right)) ||
                                presses.WasPressed(OS::GamepadTrigger::Left);
    }
} // namespace NS
