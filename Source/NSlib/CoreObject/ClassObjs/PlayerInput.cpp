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
    //! 水平 forward を XZ 平面正規化。長さ 0 の入力は世界座標の +Z にフォールバック
    [[nodiscard]] NS::Vector3 NormalizeHorizontal(const NS::Vector3& v) noexcept
    {
        NS::Vector3 out{};
        if (!NS::TryNormalizeHorizontal(v, out))
        {
            return NS::Vector3{0.0f, 0.0f, 1.0f};
        }
        return out;
    }
} // namespace

namespace NS
{
    void PlayerInput::SetCameraForward(const Vector3& cameraForwardHorizontal) noexcept
    {
        m_cameraForward = NormalizeHorizontal(cameraForwardHorizontal);
    }

    void PlayerInput::SetDesiredMove(const Vector3& worldDir, float speedScale01) noexcept
    {
        m_desiredDir = worldDir;
        m_desiredSpeedScale = Clamp(speedScale01, 0.0f, 1.0f);
    }

    void PlayerInput::SetClimbMove(float localRight, float localForward) noexcept
    {
        m_climbRight = Clamp(localRight, -1.0f, 1.0f);
        m_climbForward = Clamp(localForward, -1.0f, 1.0f);
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
        m_climbRight = 0.0f;
        m_climbForward = 0.0f;
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
        // 止めている間に世界が進まず Update が回らなかった時も、その間の押しを解いた後へ持ち越さない
        DiscardUnreadPresses();
    }

    void PlayerInput::Update(const IGetCamera* camera)
    {
        NS_ASSERT(Scene, camera != nullptr, "PlayerInput::Update: カメラを引くアクターが空");

        // camera 相対移動の基準 forward は CameraManager から読む。CameraManager 不在 (テスト等) は注入値のまま
        if (camera->GetCameraManager() != nullptr)
        {
            m_cameraForward = NormalizeHorizontal(CalcCameraForwardHorizontal(camera));
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

        const Vector3& fwd = m_cameraForward;
        const Vector3 right{fwd.z, 0.0f, -fwd.x};

        const Vector3 worldDir{
            right.x * localX + fwd.x * localZ,
            0.0f,
            right.z * localX + fwd.z * localZ,
        };

        const bool jumpPressed =
            (!wantKb && presses.WasPressed(OS::Key::Space)) || presses.WasPressed(OS::GamepadButton::A);
        const bool jumpHeld = (!wantKb && kb.IsHeld(OS::Key::Space)) || pad.IsHeld(OS::GamepadButton::A);

        m_desiredDir = worldDir;
        m_desiredSpeedScale = speedScale;
        m_climbRight = localX;
        m_climbForward = localZ;
        m_jumpPressed = m_jumpPressed || jumpPressed;
        m_jumpHeld = jumpHeld;
        m_respawnPressed =
            m_respawnPressed || (!wantKb && presses.WasPressed(OS::Key::R)) || presses.WasPressed(OS::GamepadButton::Back);

        // 体当たりのマウス左は、ゲームがマウスのボタンを受け取っている間だけ数える
        const bool mouseLeftToGame = input.GameReceivesMouseButton(OS::MouseButton::Left);
        const bool slamHeld =
            (mouseLeftToGame && input.Mouse().IsHeld(OS::MouseButton::Left)) || pad.IsHeld(OS::GamepadButton::X);
        // 前に読んだ後に押して放した分も押したと見せる。遅い世界の読まない固定フレームに押した突進を落とさない
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
