#include "Runtime/Platform/Input.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Platform/detail/InputWin32.h"

#include <windows.h>

#include <Xinput.h>

#include <cmath>
#include <utility>

namespace NS::Platform
{

    namespace
    {
        // 整数キャスト経由の不正値による m_current / m_previous の境界外アクセスを防ぐ
        [[nodiscard]] constexpr bool IsValidKey(Key k) noexcept
        {
            const std::size_t i = static_cast<std::size_t>(k);
            return i > static_cast<std::size_t>(Key::Unknown) && i < static_cast<std::size_t>(Key::Count);
        }

        // Keyと同様に境界外アクセスを防ぐ
        [[nodiscard]] constexpr bool IsValidButton(MouseButton b) noexcept
        {
            const std::size_t i = static_cast<std::size_t>(b);
            return i < static_cast<std::size_t>(MouseButton::Count);
        }

        [[nodiscard]] constexpr bool IsValidGamepadButton(GamepadButton b) noexcept
        {
            const std::size_t i = static_cast<std::size_t>(b);
            return i < static_cast<std::size_t>(GamepadButton::Count);
        }

        // スティック値を -1.0〜1.0 へ正規化
        [[nodiscard]] Stick NormalizeStick(short rawX, short rawY, unsigned short deadzone) noexcept
        {
            const float fx = [&]() -> float {
                if (rawX < 0)
                {
                    return static_cast<float>(rawX) / 32768.0f;
                }
                return static_cast<float>(rawX) / 32767.0f;
            }();
            const float fy = [&]() -> float {
                if (rawY < 0)
                {
                    return static_cast<float>(rawY) / 32768.0f;
                }
                return static_cast<float>(rawY) / 32767.0f;
            }();
            const float dz = static_cast<float>(deadzone) / 32767.0f;
            float clampedX = fx;
            if (std::fabs(fx) < dz)
            {
                clampedX = 0.0f;
            }
            float clampedY = fy;
            if (std::fabs(fy) < dz)
            {
                clampedY = 0.0f;
            }
            return Stick{clampedX, clampedY};
        }

        [[nodiscard]] float NormalizeTrigger(std::uint8_t raw) noexcept
        {
            if (raw < XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
            {
                return 0.0f;
            }
            return static_cast<float>(raw) / 255.0f;
        }

        // 0.0〜1.0 のモーターの速さを XInput の 0〜65535 へ写す
        [[nodiscard]] std::uint16_t ToMotorSpeed(float speed01) noexcept
        {
            return static_cast<std::uint16_t>(std::lround(speed01 * 65535.0f));
        }

        // 送れた場合 true。繋がっていない番号は失敗で返る
        [[nodiscard]] bool SendMotorSpeeds(int userIndex, std::uint16_t left, std::uint16_t right) noexcept
        {
            XINPUT_VIBRATION vibration{};
            vibration.wLeftMotorSpeed = left;
            vibration.wRightMotorSpeed = right;
            return ::XInputSetState(static_cast<DWORD>(userIndex), &vibration) == ERROR_SUCCESS;
        }

        // XINPUT_GAMEPAD::wButtons と GamepadButton の対応表
        constexpr std::array<unsigned short, static_cast<std::size_t>(GamepadButton::Count)> k_ButtonBits{
            XINPUT_GAMEPAD_A,
            XINPUT_GAMEPAD_B,
            XINPUT_GAMEPAD_X,
            XINPUT_GAMEPAD_Y,
            XINPUT_GAMEPAD_LEFT_SHOULDER,
            XINPUT_GAMEPAD_RIGHT_SHOULDER,
            XINPUT_GAMEPAD_BACK,
            XINPUT_GAMEPAD_START,
            XINPUT_GAMEPAD_LEFT_THUMB,
            XINPUT_GAMEPAD_RIGHT_THUMB,
            XINPUT_GAMEPAD_DPAD_UP,
            XINPUT_GAMEPAD_DPAD_DOWN,
            XINPUT_GAMEPAD_DPAD_LEFT,
            XINPUT_GAMEPAD_DPAD_RIGHT,
        };
        static_assert(k_ButtonBits.size() == static_cast<std::size_t>(GamepadButton::Count),
                      "k_ButtonBits は GamepadButton 全要素に対応する必要があります");
    } // namespace

    bool Keyboard::IsPressed(Key k) const noexcept
    {
        if (!IsValidKey(k))
        {
            return false;
        }
        const std::size_t i = static_cast<std::size_t>(k);
        return !m_previous[i] && m_current[i];
    }

    bool Keyboard::IsHeld(Key k) const noexcept
    {
        if (!IsValidKey(k))
        {
            return false;
        }
        return m_current[static_cast<std::size_t>(k)];
    }

    bool Keyboard::IsReleased(Key k) const noexcept
    {
        if (!IsValidKey(k))
        {
            return false;
        }
        const std::size_t i = static_cast<std::size_t>(k);
        return m_previous[i] && !m_current[i];
    }

    void Keyboard::Update() noexcept
    {
        m_previous = m_current;
    }

    void Keyboard::OnKeyDown(Key k) noexcept
    {
        if (!IsValidKey(k))
        {
            return;
        }
        m_current[static_cast<std::size_t>(k)] = true;
    }

    void Keyboard::OnKeyUp(Key k) noexcept
    {
        if (!IsValidKey(k))
        {
            return;
        }
        m_current[static_cast<std::size_t>(k)] = false;
    }

    void Keyboard::ClearState() noexcept
    {
        m_current.fill(false);
    }

    bool Mouse::IsPressed(MouseButton b) const noexcept
    {
        if (!IsValidButton(b))
        {
            return false;
        }
        const std::size_t i = static_cast<std::size_t>(b);
        return !m_previous[i] && m_current[i];
    }

    bool Mouse::IsHeld(MouseButton b) const noexcept
    {
        if (!IsValidButton(b))
        {
            return false;
        }
        return m_current[static_cast<std::size_t>(b)];
    }

    bool Mouse::IsReleased(MouseButton b) const noexcept
    {
        if (!IsValidButton(b))
        {
            return false;
        }
        const std::size_t i = static_cast<std::size_t>(b);
        return m_previous[i] && !m_current[i];
    }

    void Mouse::Update() noexcept
    {
        m_previous = m_current;
        m_prevX = m_x;
        m_prevY = m_y;
        m_wheel = 0;
        m_rawDeltaX = 0;
        m_rawDeltaY = 0;
    }

    void Mouse::OnMove(int x, int y) noexcept
    {
        m_x = x;
        m_y = y;
    }

    void Mouse::OnButtonDown(MouseButton b) noexcept
    {
        if (!IsValidButton(b))
        {
            return;
        }
        m_current[static_cast<std::size_t>(b)] = true;
    }

    void Mouse::OnButtonUp(MouseButton b) noexcept
    {
        if (!IsValidButton(b))
        {
            return;
        }
        m_current[static_cast<std::size_t>(b)] = false;
    }

    void Mouse::OnWheel(int delta) noexcept
    {
        m_wheel += delta;
    }

    void Mouse::SetRelativeMode(bool enabled) noexcept
    {
        m_relativeMode = enabled;
        // モード切替時に積み残しの相対量を捨て、切替直後の 1 フレームが暴れないようにする
        m_rawDeltaX = 0;
        m_rawDeltaY = 0;
        m_absOriginSet = false;
    }

    void Mouse::OnRawMove(int dx, int dy) noexcept
    {
        m_rawDeltaX += dx;
        m_rawDeltaY += dy;
    }

    void Mouse::OnRawMoveAbsolute(int screenX, int screenY) noexcept
    {
        if (m_absOriginSet)
        {
            m_rawDeltaX += screenX - m_absX;
            m_rawDeltaY += screenY - m_absY;
        }
        m_absX = screenX;
        m_absY = screenY;
        m_absOriginSet = true;
    }

    void Mouse::ClearState() noexcept
    {
        m_current.fill(false);
        m_wheel = 0;
        m_rawDeltaX = 0;
        m_rawDeltaY = 0;
        m_absOriginSet = false;
    }

    Gamepad::Gamepad(int userIndex) noexcept : m_userIndex(userIndex) {}

    bool Gamepad::IsConnected() const noexcept
    {
        return m_connected;
    }

    bool Gamepad::IsPressed(GamepadButton b) const noexcept
    {
        if (!IsValidGamepadButton(b))
        {
            return false;
        }
        const std::size_t i = static_cast<std::size_t>(b);
        return !m_previous[i] && m_current[i];
    }

    bool Gamepad::IsHeld(GamepadButton b) const noexcept
    {
        if (!IsValidGamepadButton(b))
        {
            return false;
        }
        return m_current[static_cast<std::size_t>(b)];
    }

    bool Gamepad::IsReleased(GamepadButton b) const noexcept
    {
        if (!IsValidGamepadButton(b))
        {
            return false;
        }
        const std::size_t i = static_cast<std::size_t>(b);
        return m_previous[i] && !m_current[i];
    }

    Stick Gamepad::LeftStick() const noexcept
    {
        return m_leftStick;
    }

    Stick Gamepad::RightStick() const noexcept
    {
        return m_rightStick;
    }

    float Gamepad::LeftTrigger() const noexcept
    {
        return m_leftTrigger;
    }

    float Gamepad::RightTrigger() const noexcept
    {
        return m_rightTrigger;
    }

    int Gamepad::UserIndex() const noexcept
    {
        return m_userIndex;
    }

    bool Gamepad::SetVibration(float left, float right) noexcept
    {
        // 非数は比較が偽になり、範囲の内側の判定を通らない
        if (!(left >= 0.0f && left <= 1.0f) || !(right >= 0.0f && right <= 1.0f))
        {
            return false;
        }
        m_vibration = GamepadVibration{left, right};
        m_vibrationWritten = true;
        return true;
    }

    GamepadVibration Gamepad::Vibration() const noexcept
    {
        return m_vibration;
    }

    void Gamepad::StopVibration() noexcept
    {
        m_vibration = GamepadVibration{};
        m_vibrationWritten = false;
        if (m_sentLeftMotor == 0 && m_sentRightMotor == 0)
        {
            return;
        }
        if (SendMotorSpeeds(m_userIndex, 0, 0))
        {
            m_sentLeftMotor = 0;
            m_sentRightMotor = 0;
        }
    }

    void Gamepad::Update() noexcept
    {
        m_previous = m_current;

        // 一時停止やプレイの終わりで書くのが止まると、ここで振動も止まる
        if (!m_vibrationWritten)
        {
            m_vibration = GamepadVibration{};
        }
        m_vibrationWritten = false;

        XINPUT_STATE state{};
        const DWORD result = ::XInputGetState(static_cast<DWORD>(m_userIndex), &state);
        if (result != ERROR_SUCCESS)
        {
            m_connected = false;
            m_current.fill(false);
            m_leftStick = Stick{};
            m_rightStick = Stick{};
            m_leftTrigger = 0.0f;
            m_rightTrigger = 0.0f;
            // 繋がり直したパッドへは今の速さを送り直す
            m_sentLeftMotor = 0;
            m_sentRightMotor = 0;
            return;
        }

        m_connected = true;
        for (std::size_t i = 0; i < k_ButtonBits.size(); ++i)
        {
            m_current[i] = (state.Gamepad.wButtons & k_ButtonBits[i]) != 0;
        }
        m_leftStick =
            NormalizeStick(state.Gamepad.sThumbLX, state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        m_rightStick =
            NormalizeStick(state.Gamepad.sThumbRX, state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        m_leftTrigger = NormalizeTrigger(state.Gamepad.bLeftTrigger);
        m_rightTrigger = NormalizeTrigger(state.Gamepad.bRightTrigger);

        // 同じ速さを毎フレーム送り直さない
        const std::uint16_t leftMotor = ToMotorSpeed(m_vibration.left);
        const std::uint16_t rightMotor = ToMotorSpeed(m_vibration.right);
        if (leftMotor == m_sentLeftMotor && rightMotor == m_sentRightMotor)
        {
            return;
        }
        if (SendMotorSpeeds(m_userIndex, leftMotor, rightMotor))
        {
            m_sentLeftMotor = leftMotor;
            m_sentRightMotor = rightMotor;
        }
    }

    Input::Input() noexcept
    {
        for (std::size_t i = 0; i < m_gamepads.size(); ++i)
        {
            m_gamepads[i] = ::NS::Platform::Gamepad{static_cast<int>(i)};
        }
    }

    Gamepad& Input::Gamepad(int index) noexcept
    {
        if (index < 0 || static_cast<std::size_t>(index) >= m_gamepads.size())
        {
            return m_gamepads[0];
        }
        return m_gamepads[static_cast<std::size_t>(index)];
    }

    const Gamepad& Input::Gamepad(int index) const noexcept
    {
        if (index < 0 || static_cast<std::size_t>(index) >= m_gamepads.size())
        {
            return m_gamepads[0];
        }
        return m_gamepads[static_cast<std::size_t>(index)];
    }

    bool Input::SetGamepadUserIndex(int userIndex) noexcept
    {
        if (userIndex < 0 || userIndex >= static_cast<int>(XUSER_MAX_COUNT))
        {
            return false;
        }
        // 向け直した後は前の番号の Update が来ないので、前の番号の振動をここで止める
        m_gamepads[0].StopVibration();
        m_gamepads[0] = ::NS::Platform::Gamepad{userIndex};
        return true;
    }

    void Input::Update() noexcept
    {
        m_keyboard.Update();
        m_mouse.Update();
        for (NS::Platform::Gamepad& pad : m_gamepads)
        {
            pad.Update();
        }
    }

    void Input::BeginNeutral() noexcept
    {
        ++m_neutralDepth;
        if (m_neutralDepth > 1)
        {
            return;
        }
        // 控えの側を中立の物に作り直してから入れ替える。入れ替えた後の控えが手元の状態
        m_heldKeyboard = NS::Platform::Keyboard{};
        m_heldMouse = NS::Platform::Mouse{};
        std::swap(m_keyboard, m_heldKeyboard);
        std::swap(m_mouse, m_heldMouse);
        for (std::size_t i = 0; i < m_gamepads.size(); ++i)
        {
            // 中立のパッドは Update を受けないので機器を読まず、送った速さが 0 のまま止めても機器へ書かない
            m_heldGamepads[i] = ::NS::Platform::Gamepad{static_cast<int>(i)};
            std::swap(m_gamepads[i], m_heldGamepads[i]);
        }
    }

    void Input::EndNeutral() noexcept
    {
        if (m_neutralDepth == 0)
        {
            return;
        }
        --m_neutralDepth;
        if (m_neutralDepth > 0)
        {
            return;
        }
        std::swap(m_keyboard, m_heldKeyboard);
        std::swap(m_mouse, m_heldMouse);
        for (std::size_t i = 0; i < m_gamepads.size(); ++i)
        {
            std::swap(m_gamepads[i], m_heldGamepads[i]);
        }
    }

    Key MapVkToKey(unsigned int vk) noexcept
    {
        if (vk >= 'A' && vk <= 'Z')
        {
            return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(vk - 'A'));
        }
        if (vk >= '0' && vk <= '9')
        {
            return static_cast<Key>(static_cast<int>(Key::Num0) + static_cast<int>(vk - '0'));
        }
        if (vk >= VK_F1 && vk <= VK_F12)
        {
            return static_cast<Key>(static_cast<int>(Key::F1) + static_cast<int>(vk - VK_F1));
        }

        // 記号・制御キーの個別対応
        switch (vk)
        {
        case VK_LEFT:
            return Key::Left;
        case VK_RIGHT:
            return Key::Right;
        case VK_UP:
            return Key::Up;
        case VK_DOWN:
            return Key::Down;
        case VK_SPACE:
            return Key::Space;
        case VK_RETURN:
            return Key::Enter;
        case VK_ESCAPE:
            return Key::Escape;
        case VK_TAB:
            return Key::Tab;
        case VK_BACK:
            return Key::Backspace;
        case VK_DELETE:
            return Key::Delete;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return Key::Shift;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return Key::Ctrl;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return Key::Alt;
        default:
            return Key::Unknown;
        }
    }

    void DispatchWin32MessageToInput(Input& input,
                                     unsigned int msg,
                                     std::uintptr_t wparam,
                                     std::intptr_t lparam) noexcept
    {
        switch (msg)
        {
        // キーボード
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            // lparam のスキャンコード、bit 30 のリピートフラグ、拡張キーは現状未使用
            const Key k = MapVkToKey(static_cast<unsigned int>(wparam));
            input.Keyboard().OnKeyDown(k);
            break;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            const Key k = MapVkToKey(static_cast<unsigned int>(wparam));
            input.Keyboard().OnKeyUp(k);
            break;
        }
        // フォーカス喪失
        case WM_KILLFOCUS:
        {
            input.Keyboard().ClearState();
            input.Mouse().ClearState();
            break;
        }
        // マウス
        case WM_MOUSEMOVE:
        {
            const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
            const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
            input.Mouse().OnMove(x, y);
            break;
        }
        case WM_LBUTTONDOWN:
            input.Mouse().OnButtonDown(MouseButton::Left);
            break;
        case WM_LBUTTONUP:
            input.Mouse().OnButtonUp(MouseButton::Left);
            break;
        case WM_RBUTTONDOWN:
            input.Mouse().OnButtonDown(MouseButton::Right);
            break;
        case WM_RBUTTONUP:
            input.Mouse().OnButtonUp(MouseButton::Right);
            break;
        case WM_MBUTTONDOWN:
            input.Mouse().OnButtonDown(MouseButton::Middle);
            break;
        case WM_MBUTTONUP:
            input.Mouse().OnButtonUp(MouseButton::Middle);
            break;
        case WM_XBUTTONDOWN:
        {
            const WORD xb = GET_XBUTTON_WPARAM(wparam);
            if (xb == XBUTTON1)
            {
                input.Mouse().OnButtonDown(MouseButton::X1);
            }
            else if (xb == XBUTTON2)
            {
                input.Mouse().OnButtonDown(MouseButton::X2);
            }
            break;
        }
        case WM_XBUTTONUP:
        {
            const WORD xb = GET_XBUTTON_WPARAM(wparam);
            if (xb == XBUTTON1)
            {
                input.Mouse().OnButtonUp(MouseButton::X1);
            }
            else if (xb == XBUTTON2)
            {
                input.Mouse().OnButtonUp(MouseButton::X2);
            }
            break;
        }
        case WM_MOUSEWHEEL:
        {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            input.Mouse().OnWheel(delta);
            break;
        }
        // Raw Input の相対移動
        case WM_INPUT:
        {
            RAWINPUT raw{};
            UINT size = sizeof(raw);
            if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(static_cast<LPARAM>(lparam)),
                                  RID_INPUT,
                                  &raw,
                                  &size,
                                  sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
            {
                break;
            }
            if (raw.header.dwType != RIM_TYPEMOUSE)
            {
                break;
            }
            if ((raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
            {
                input.Mouse().OnRawMove(static_cast<int>(raw.data.mouse.lLastX),
                                        static_cast<int>(raw.data.mouse.lLastY));
                break;
            }
            // 遠隔操作とタブレットは絶対座標で届く。相対モードの差分は移動量だけなので座標差から作る
            // 座標は 0〜65535 の正規化値。画面の画素へ戻さないと感度が画面の大きさで変わる
            int widthMetric = SM_CXSCREEN;
            int heightMetric = SM_CYSCREEN;
            if ((raw.data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0)
            {
                widthMetric = SM_CXVIRTUALSCREEN;
                heightMetric = SM_CYVIRTUALSCREEN;
            }
            const float width = static_cast<float>(::GetSystemMetrics(widthMetric));
            const float height = static_cast<float>(::GetSystemMetrics(heightMetric));
            const float normalizedX = static_cast<float>(raw.data.mouse.lLastX) / 65535.0f;
            const float normalizedY = static_cast<float>(raw.data.mouse.lLastY) / 65535.0f;
            // 遠隔で視点が動かない時に、入力が届いていないのか計算が違うのかを切り分ける
            // 毎回出すとマウスを動かす間ずっとログが流れるので 1 回で止める
            static bool s_absoluteReported = false;
            if (!s_absoluteReported)
            {
                s_absoluteReported = true;
                NS_LOG_INFO(Platform, "絶対座標のマウスを検出、 相対移動は座標差から作る");
            }
            input.Mouse().OnRawMoveAbsolute(static_cast<int>(normalizedX * width),
                                            static_cast<int>(normalizedY * height));
            break;
        }
        default:
            break;
        }
    }

} // namespace NS::Platform
