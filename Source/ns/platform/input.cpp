#include <ns/platform/input.h>

#include <ns/platform/detail/input_win32.h>

#include <windows.h>

namespace ns::platform
{

    namespace
    {
        /// 配列インデックスとして安全な Key 値か判定する。
        /// `enum class : int` の整数キャスト経由で不正値 (Unknown 以下 / kCount 以上 / 負値)
        /// が来ても m_current/m_previous の境界外アクセスを防ぐ。
        [[nodiscard]] constexpr bool IsValidKey(Key k) noexcept
        {
            const auto i = static_cast<std::size_t>(k);
            return i > static_cast<std::size_t>(Key::Unknown) && i < static_cast<std::size_t>(Key::kCount);
        }

        /// MouseButton 用の境界チェック。Key と同じ理由で必要。
        /// MouseButton::Left = 0 始まりなので i >= 0 && i < kCount で判定。
        [[nodiscard]] constexpr bool IsValidButton(MouseButton b) noexcept
        {
            const auto i = static_cast<std::size_t>(b);
            return i < static_cast<std::size_t>(MouseButton::kCount);
        }
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

    void Input::Update() noexcept
    {
        m_keyboard.Update();
        m_mouse.Update();
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
            // lparam: scan code / repeat flag (bit 30) / extended key —  では未使用
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

} // namespace ns::platform
