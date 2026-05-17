#include <ns/platform/input.h>

#include <ns/platform/detail/input_win32.h>

#include <windows.h>

namespace ns::platform
{

    namespace
    {
        [[nodiscard]] std::size_t KeyIndex(Key k) noexcept
        {
            return static_cast<std::size_t>(k);
        }
    } // namespace

    bool Keyboard::IsPressed(Key k) const noexcept
    {
        if (k == Key::Unknown || k == Key::kCount)
        {
            return false;
        }
        const std::size_t i = KeyIndex(k);
        return !m_previous[i] && m_current[i];
    }

    bool Keyboard::IsHeld(Key k) const noexcept
    {
        if (k == Key::Unknown || k == Key::kCount)
        {
            return false;
        }
        return m_current[KeyIndex(k)];
    }

    bool Keyboard::IsReleased(Key k) const noexcept
    {
        if (k == Key::Unknown || k == Key::kCount)
        {
            return false;
        }
        const std::size_t i = KeyIndex(k);
        return m_previous[i] && !m_current[i];
    }

    void Keyboard::Update() noexcept
    {
        m_previous = m_current;
    }

    void Keyboard::OnKeyDown(Key k) noexcept
    {
        if (k == Key::Unknown || k == Key::kCount)
        {
            return;
        }
        m_current[KeyIndex(k)] = true;
    }

    void Keyboard::OnKeyUp(Key k) noexcept
    {
        if (k == Key::Unknown || k == Key::kCount)
        {
            return;
        }
        m_current[KeyIndex(k)] = false;
    }

    void Keyboard::ClearState() noexcept
    {
        m_current.fill(false);
    }

    void Input::Update() noexcept
    {
        m_keyboard.Update();
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
        (void)lparam;
        switch (msg)
        {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
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
            break;
        }
        default:
            break;
        }
    }

} // namespace ns::platform
