#include <Framework/Platform/Input.h>

#include <Framework/Platform/detail/input_win32.h>

#include "Framework/Framework.h"

#include <Xinput.h>

#include <cmath>
#include <cstdint>

namespace NS::Platform
{

    namespace
    {
        /// 配列インデックスとして安全な Key 値か判定する
        /// `enum class : int` の整数キャスト経由で不正値 (Unknown 以下 / kCount 以上 / 負値)
        /// が来ても m_current/m_previous の境界外アクセスを防ぐ
        [[nodiscard]] constexpr bool IsValidKey(Key k) noexcept
        {
            const auto i = static_cast<std::size_t>(k);
            return i > static_cast<std::size_t>(Key::Unknown) && i < static_cast<std::size_t>(Key::kCount);
        }

        /// MouseButton 用の境界チェック。Key と同じ理由で必要
        /// MouseButton::Left = 0 始まりなので i >= 0 && i < kCount で判定
        [[nodiscard]] constexpr bool IsValidButton(MouseButton b) noexcept
        {
            const auto i = static_cast<std::size_t>(b);
            return i < static_cast<std::size_t>(MouseButton::kCount);
        }

        [[nodiscard]] constexpr bool IsValidGamepadButton(GamepadButton b) noexcept
        {
            const auto i = static_cast<std::size_t>(b);
            return i < static_cast<std::size_t>(GamepadButton::kCount);
        }

        /// XInput の SHORT スティック生値を -1.0〜1.0 に正規化し、軸別デッドゾーンを適用
        /// 負値は /32768、正値は /32767 で対称的にマップする
        /// ラジアルではなく軸別デッドゾーン (Mario 系の縦横独立操作向け)
        /// deadzone は符号なし: 負値で「全入力が deadzone 越え」と誤判定されるのを防ぐ
        [[nodiscard]] Stick NormalizeStick(short rawX, short rawY, unsigned short deadzone) noexcept
        {
            const float fx = (rawX < 0) ? static_cast<float>(rawX) / 32768.0f : static_cast<float>(rawX) / 32767.0f;
            const float fy = (rawY < 0) ? static_cast<float>(rawY) / 32768.0f : static_cast<float>(rawY) / 32767.0f;
            const float dz = static_cast<float>(deadzone) / 32767.0f;
            return Stick{
                std::fabs(fx) < dz ? 0.0f : fx,
                std::fabs(fy) < dz ? 0.0f : fy,
            };
        }

        /// 0〜255 の BYTE トリガー生値を 0.0〜1.0 に正規化、スレッショルド未満は 0.0
        [[nodiscard]] float NormalizeTrigger(std::uint8_t raw) noexcept
        {
            if (raw < XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
            {
                return 0.0f;
            }
            return static_cast<float>(raw) / 255.0f;
        }

        /// XINPUT_GAMEPAD::wButtons のビットマスクと GamepadButton enum の対応表
        /// 添字順は GamepadButton 宣言順と一致させる
        constexpr std::array<unsigned short, static_cast<std::size_t>(GamepadButton::kCount)> kButtonBits{
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
        static_assert(kButtonBits.size() == static_cast<std::size_t>(GamepadButton::kCount),
                      "kButtonBits は GamepadButton 全要素に対応する必要があります");
    } // namespace

    bool Keyboard::IsPressed(Key k) const noexcept
    {
        if (!IsValidKey(k))
        {
            return false;
        }
        const auto i = static_cast<std::size_t>(k);
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
        const auto i = static_cast<std::size_t>(k);
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
        const auto i = static_cast<std::size_t>(b);
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
        const auto i = static_cast<std::size_t>(b);
        return m_previous[i] && !m_current[i];
    }

    void Mouse::Update() noexcept
    {
        m_previous = m_current;
        m_prevX = m_x;
        m_prevY = m_y;
        m_wheel = 0;
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

    void Mouse::ClearState() noexcept
    {
        m_current.fill(false);
        m_wheel = 0;
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
        const auto i = static_cast<std::size_t>(b);
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
        const auto i = static_cast<std::size_t>(b);
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

    void Gamepad::Update() noexcept
    {
        m_previous = m_current;

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
            return;
        }

        m_connected = true;
        for (std::size_t i = 0; i < kButtonBits.size(); ++i)
        {
            m_current[i] = (state.Gamepad.wButtons & kButtonBits[i]) != 0;
        }
        m_leftStick =
            NormalizeStick(state.Gamepad.sThumbLX, state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        m_rightStick =
            NormalizeStick(state.Gamepad.sThumbRX, state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        m_leftTrigger = NormalizeTrigger(state.Gamepad.bLeftTrigger);
        m_rightTrigger = NormalizeTrigger(state.Gamepad.bRightTrigger);
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

    void Input::Update() noexcept
    {
        m_keyboard.Update();
        m_mouse.Update();
        for (auto& pad : m_gamepads)
        {
            pad.Update();
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
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            // lparam: scan code / repeat flag (bit 30) / extended key — 現状未使用
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
        case WM_KILLFOCUS:
        {
            input.Keyboard().ClearState();
            input.Mouse().ClearState();
            break;
        }
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
            const auto xb = GET_XBUTTON_WPARAM(wparam);
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
            const auto xb = GET_XBUTTON_WPARAM(wparam);
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
        default:
            break;
        }
    }

} // namespace NS::Platform
