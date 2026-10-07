#include "NSlib/Windows/Window.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Windows/Input.h"
#include "NSlib/Windows/StringUtils.h"
#include "NSlib/Windows/detail/InputWin32.h"

namespace NS::OS
{

    namespace
    {
        // Window は単一インスタンス
        Window* s_instance = nullptr;

        constexpr wchar_t k_ClassName[] = L"NS_Window";

        // キーボード系メッセージ
        [[nodiscard]] constexpr bool IsKeyboardMessage(UINT msg) noexcept
        {
            return msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP;
        }

        // マウス系メッセージ
        [[nodiscard]] constexpr bool IsMouseMessage(UINT msg) noexcept
        {
            switch (msg)
            {
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_MOUSEWHEEL:
                return true;
            default:
                return false;
            }
        }

        // Input へ送る Win32 メッセージの判定
        [[nodiscard]] constexpr bool IsInputMessage(UINT msg) noexcept
        {
            return IsKeyboardMessage(msg) || IsMouseMessage(msg) || msg == WM_KILLFOCUS;
        }

        // 離しのメッセージ
        [[nodiscard]] constexpr bool IsReleaseMessage(UINT msg) noexcept
        {
            switch (msg)
            {
            case WM_KEYUP:
            case WM_SYSKEYUP:
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
            case WM_XBUTTONUP:
                return true;
            default:
                return false;
            }
        }

        // UI が取っていてゲームへ流さない入力か
        // 左ボタンの押下は、UI がマウスを持つ間も左の受け渡しが登録されていれば流す
        [[nodiscard]] bool IsTakenByUi(const Input& input, UINT msg) noexcept
        {
            if (IsKeyboardMessage(msg))
            {
                return input.UiWantsKeyboard();
            }
            if (msg == WM_LBUTTONDOWN)
            {
                return !input.GameReceivesMouseButton(MouseButton::Left);
            }
            if (IsMouseMessage(msg))
            {
                return input.UiWantsMouse();
            }
            return false;
        }

    } // namespace

    LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        Window* window = s_instance;
        if (window == nullptr)
        {
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        // 焦点を失ったまま毎フレーム SetCursorPos すると他のアプリの操作を奪うため、焦点の有無を控える
        if (msg == WM_SETFOCUS)
        {
            window->m_hasFocus = true;
        }
        else if (msg == WM_KILLFOCUS)
        {
            window->m_hasFocus = false;
        }

        // OSからのメッセージを ImGui に送る
        if (window->m_messageHook)
        {
            window->m_messageHook(hwnd, msg, wparam, lparam);
        }

        // 入力メッセージは Input へ振り分け
        if (IsInputMessage(msg))
        {
            if (window->m_input != nullptr)
            {
                // UIがキャプチャ中の入力はゲーム側へ流さない
                // 離しだけは流す。奪うと Input に押しっぱなしが残り、離した瞬間の入力が来なくなる
                if (!IsReleaseMessage(msg) && IsTakenByUi(*window->m_input, msg))
                {
                    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
                }

                DispatchWin32MessageToInput(*window->m_input, msg, wparam, lparam);
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        // ウィンドウ管理メッセージ
        switch (msg)
        {
        case WM_ERASEBKGND:
        {
            // DX11 が毎フレーム Present するので GDI の背景消去は要らない
            // 通すと初回 Present の前に白く光るため 1 を返して止める
            return 1;
        }
        case WM_SIZE:
        {
            if (wparam == SIZE_MINIMIZED)
            {
                break;
            }
            window->m_size.width = LOWORD(lparam);
            window->m_size.height = HIWORD(lparam);
            if (window->m_onResize)
            {
                window->m_onResize(window->m_size);
            }
            break;
        }
        case WM_SETCURSOR:
        {
            // 枠やタイトルバーは既定カーソルを使用するため HTCLIENT のみ対象とする
            if (LOWORD(lparam) == HTCLIENT && !window->m_cursorVisible)
            {
                ::SetCursor(nullptr);
                return TRUE;
            }
            break;
        }
        case WM_INPUT:
        {
            if (window->m_input != nullptr)
            {
                DispatchWin32MessageToInput(*window->m_input, msg, wparam, lparam);
            }
            break;
        }
        case WM_CLOSE:
        {
            if (window->m_onClose)
            {
                window->m_onClose();
            }
            else
            {
                ::PostQuitMessage(0);
            }
            return 0;
        }
        case WM_DESTROY:
        {
            ::PostQuitMessage(0);
            return 0;
        }
        default:
            break;
        }
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    Window::Window(const WindowDesc& desc)
    {
        if (s_instance != nullptr)
        {
            NS_LOG_FATAL(Platform, "Window は単一インスタンス前提です (二重生成)");
        }
        s_instance = this;

        ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        m_hInstance = ::GetModuleHandleW(nullptr);
        m_size = desc.size;

        // ウィンドウクラス登録
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = m_hInstance;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        // 背景消去を無効化し、初回Present時の白フラッシュを回避する
        wc.hbrBackground = nullptr;
        wc.lpszClassName = k_ClassName;

        m_classAtom = ::RegisterClassExW(&wc);
        if (m_classAtom == 0)
        {
            NS_LOG_ERROR(Platform, "RegisterClassExW 失敗 (GetLastError={})", ::GetLastError());
            s_instance = nullptr;
            return;
        }

        // ウィンドウ生成
        RECT rect{0, 0, desc.size.width, desc.size.height};
        const DWORD style = WS_OVERLAPPEDWINDOW;
        const DWORD exStyle = 0;
        ::AdjustWindowRectEx(&rect, style, FALSE, exStyle);

        const std::wstring wideTitle = ::NS::OS::StringUtils::WideFromUtf8(desc.title);

        m_hwnd = ::CreateWindowExW(exStyle,
                                   k_ClassName,
                                   wideTitle.c_str(),
                                   style,
                                   CW_USEDEFAULT,
                                   CW_USEDEFAULT,
                                   rect.right - rect.left,
                                   rect.bottom - rect.top,
                                   nullptr,
                                   nullptr,
                                   m_hInstance,
                                   nullptr);

        if (m_hwnd == nullptr)
        {
            NS_LOG_ERROR(Platform, "CreateWindowExW 失敗 (GetLastError={})", ::GetLastError());
            ::UnregisterClassW(k_ClassName, m_hInstance);
            m_classAtom = 0;
            s_instance = nullptr;
            return;
        }

        // Raw Input のマウス登録
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage = 0x02;
        rid.dwFlags = 0;
        rid.hwndTarget = m_hwnd;
        if (::RegisterRawInputDevices(&rid, 1, sizeof(rid)) == FALSE)
        {
            NS_LOG_ERROR(
                Platform, "RegisterRawInputDevices 失敗、 相対マウスは無効 (GetLastError={})", ::GetLastError());
        }

        int showCommand = SW_HIDE;
        if (desc.visible)
        {
            showCommand = SW_SHOW;
        }
        ::ShowWindow(m_hwnd, showCommand);
        ::UpdateWindow(m_hwnd);

        m_hasFocus = (::GetForegroundWindow() == m_hwnd);
    }

    Window::~Window()
    {
        if (m_hwnd != nullptr)
        {
            ::DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
        if (m_classAtom != 0)
        {
            ::UnregisterClassW(k_ClassName, m_hInstance);
            m_classAtom = 0;
        }
        s_instance = nullptr;
    }

    bool Window::IsValid() const noexcept
    {
        return m_hwnd != nullptr;
    }

    void Window::PollMessages() noexcept
    {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                m_shouldClose = true;
                continue;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }

        // 相対マウスは WM_INPUT の生の移動量で動くので、毎フレーム固定点へ戻してもカメラ操作は壊れない
        if (m_cursorLocked && m_hasFocus && m_hwnd != nullptr)
        {
            POINT point = m_lockPoint.value_or(POINT{m_size.width / 2, m_size.height / 2});
            ::ClientToScreen(m_hwnd, &point);
            ::SetCursorPos(point.x, point.y);
        }
    }

    bool Window::ShouldClose() const noexcept
    {
        return m_shouldClose;
    }

    ::NS::Size2D Window::Size() const noexcept
    {
        return m_size;
    }

    HWND Window::NativeHandle() const noexcept
    {
        return m_hwnd;
    }

    void Window::SetTitle(std::string_view utf8Title) noexcept
    {
        if (m_hwnd == nullptr)
        {
            return;
        }
        const std::wstring wide = ::NS::OS::StringUtils::WideFromUtf8(utf8Title);
        ::SetWindowTextW(m_hwnd, wide.c_str());
    }

    void Window::SetCursorVisible(bool visible) noexcept
    {
        m_cursorVisible = visible;
        // 次の WM_SETCURSOR を待たず即時反映する。マウスが動かなくても切替わる
        HCURSOR cursor = nullptr;
        if (visible)
        {
            cursor = ::LoadCursorW(nullptr, IDC_ARROW);
        }
        ::SetCursor(cursor);
    }

    bool Window::IsCursorVisible() const noexcept
    {
        return m_cursorVisible;
    }

    void Window::SetCursorLocked(bool locked) noexcept
    {
        m_cursorLocked = locked;
    }

    bool Window::IsCursorLocked() const noexcept
    {
        return m_cursorLocked;
    }

    void Window::SetCursorLockPoint(int clientX, int clientY) noexcept
    {
        m_lockPoint = POINT{clientX, clientY};
    }

    void Window::RequestClose() noexcept
    {
        if (m_hwnd != nullptr)
        {
            ::PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        }
    }

    void Window::SetResizeCallback(std::function<void(::NS::Size2D)> cb)
    {
        m_onResize = std::move(cb);
    }

    void Window::SetCloseCallback(std::function<void()> cb)
    {
        m_onClose = std::move(cb);
    }

    void Window::AttachInput(Input* input) noexcept
    {
        m_input = input;
    }

    void Window::SetMessageHook(MessageHook hook)
    {
        m_messageHook = std::move(hook);
    }

} // namespace NS::OS
