#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/Windows/Gamepad.h"
#include "NSlib/Windows/Input.h"
#include "NSlib/Windows/Keyboard.h"
#include "NSlib/Windows/Mouse.h"

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

    void PlayerInput::Update(const IUseCamera* camera)
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
            (!wantKb && kb.IsPressed(OS::Key::Space)) || pad.IsPressed(OS::GamepadButton::A);
        const bool jumpHeld = (!wantKb && kb.IsHeld(OS::Key::Space)) || pad.IsHeld(OS::GamepadButton::A);

        m_desiredDir = worldDir;
        m_desiredSpeedScale = speedScale;
        m_climbRight = localX;
        m_climbForward = localZ;
        m_jumpPressed = m_jumpPressed || jumpPressed;
        m_jumpHeld = jumpHeld;
        m_respawnPressed =
            m_respawnPressed || (!wantKb && kb.IsPressed(OS::Key::R)) || pad.IsPressed(OS::GamepadButton::Back);

        // 体当たりのマウス左は、ゲームがマウスのボタンを受け取っている間だけ数える
        const bool slamHeld = (input.GameReceivesMouseButton(OS::MouseButton::Left) &&
                               input.Mouse().IsHeld(OS::MouseButton::Left)) ||
                              pad.IsHeld(OS::GamepadButton::X);
        // 遅い世界では自機が読まない歩がある。その間に押して放した分も、次に読む歩で押したと見せる
        m_slamHeld = slamHeld || m_slamSeen;
        m_slamSeen = m_slamHeld;

        // 手放しは専用のマウス右ボタン / 左トリガー。後ろ入力と兼ねると、カメラ側の縁へ寄せた入力で手を放す
        const bool mouseFree = !input.UiWantsMouse();
        m_releaseLedgePressed = m_releaseLedgePressed ||
                                (mouseFree && input.Mouse().IsPressed(OS::MouseButton::Right)) ||
                                (releaseLedgeHeld && !m_prevReleaseLedgeHeld);
        m_prevReleaseLedgeHeld = releaseLedgeHeld;
    }
} // namespace NS
