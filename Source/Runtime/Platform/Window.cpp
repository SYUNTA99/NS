#include "Runtime/Platform/Window.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/StringUtils.h"
#include "Runtime/Platform/detail/InputWin32.h"
#include "Runtime/Platform/detail/WindowWin32.h"
#include "Runtime/Platform/Input.h"

#include <memory>

namespace NS::Platform
{

    namespace
    {
        // Window は単一インスタンス
        Window::Impl* s_instance = nullptr;

        /// Inputクラス に送る Win32 メッセージ判定
        [[nodiscard]] constexpr bool IsInputMessage(UINT msg) noexcept
        {
            switch (msg)
            {
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_KILLFOCUS:
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

        LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
        {
            Window::Impl* impl = s_instance;
            if (impl == nullptr)
            {
                return ::DefWindowProcW(hwnd, msg, wparam, lparam);
            }

            // OSからのメッセージを ImGui に送る
            if (impl->messageHook)
            {
                impl->messageHook(static_cast<void*>(hwnd),
                                  static_cast<std::uint32_t>(msg),
                                  static_cast<std::uintptr_t>(wparam),
                                  static_cast<std::intptr_t>(lparam));
            }

            // 入力メッセージは Input へ振り分け
            if (IsInputMessage(msg))
            {
                if (impl->input != nullptr)
                {
                    // UIがキャプチャ中の入力はゲーム側へ流さない
                    if ((IsKeyboardMessage(msg) && impl->input->UiWantsKeyboard()) ||
                        (IsMouseMessage(msg) && impl->input->UiWantsMouse()))
                    {
                        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
                    }

                    DispatchWin32MessageToInput(*impl->input,
                                                static_cast<unsigned int>(msg),
                                                static_cast<std::uintptr_t>(wparam),
                                                static_cast<std::intptr_t>(lparam));
                }
                return ::DefWindowProcW(hwnd, msg, wparam, lparam);
            }

            // ウィンドウ管理メッセージ
            switch (msg)
            {
            case WM_ERASEBKGND:
            {
                // D3Dが毎フレームPresentするため、GDI背景消去は不要。
                // これを通すと初回Present前に白フラッシュが発生するため明示的に1を返す
                return 1;
            }
            case WM_SIZE:
            {
                if (wparam == SIZE_MINIMIZED)
                {
                    break;
                }
                impl->size.width = LOWORD(lparam);
                impl->size.height = HIWORD(lparam);
                if (impl->onResize)
                {
                    impl->onResize(impl->size);
                }
                break;
            }
            case WM_SETCURSOR:
            {
                // 枠やタイトルバーは既定カーソルを使用するため HTCLIENT のみ対象とする
                if (LOWORD(lparam) == HTCLIENT && !impl->cursorVisible)
                {
                    ::SetCursor(nullptr);
                    return TRUE;
                }
                break;
            }
            case WM_INPUT:
            {
                if (impl->input != nullptr)
                {
                    DispatchWin32MessageToInput(*impl->input,
                                                static_cast<unsigned int>(msg),
                                                static_cast<std::uintptr_t>(wparam),
                                                static_cast<std::intptr_t>(lparam));
                }
                break;
            }
            case WM_CLOSE:
            {
                if (impl->onClose)
                {
                    impl->onClose();
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
    } // namespace

    Window::Window(const WindowDesc& desc) : m_pImpl(std::make_unique<Impl>())
    {
        if (s_instance != nullptr)
        {
            NS_LOG_FATAL(Platform, "Window は単一インスタンス前提です (二重生成)");
        }
        s_instance = m_pImpl.get();

        ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        m_pImpl->hInstance = ::GetModuleHandleW(nullptr);
        m_pImpl->size = desc.size;
        m_pImpl->className = L"NS_Window";

        // ウィンドウクラス登録
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = m_pImpl->hInstance;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        // 背景消去を無効化し、初回Present時の白フラッシュを回避する
        wc.hbrBackground = nullptr;
        wc.lpszClassName = m_pImpl->className.c_str();

        m_pImpl->classAtom = ::RegisterClassExW(&wc);
        if (m_pImpl->classAtom == 0)
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

        const std::wstring wideTitle = ::NS::Core::StringUtils::WideFromUtf8(desc.title);

        m_pImpl->hwnd = ::CreateWindowExW(exStyle,
                                          m_pImpl->className.c_str(),
                                          wideTitle.c_str(),
                                          style,
                                          CW_USEDEFAULT,
                                          CW_USEDEFAULT,
                                          rect.right - rect.left,
                                          rect.bottom - rect.top,
                                          nullptr,
                                          nullptr,
                                          m_pImpl->hInstance,
                                          nullptr);

        if (m_pImpl->hwnd == nullptr)
        {
            NS_LOG_ERROR(Platform, "CreateWindowExW 失敗 (GetLastError={})", ::GetLastError());
            ::UnregisterClassW(m_pImpl->className.c_str(), m_pImpl->hInstance);
            m_pImpl->classAtom = 0;
            s_instance = nullptr;
            return;
        }

        // Raw Input のマウス登録
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage = 0x02;
        rid.dwFlags = 0;
        rid.hwndTarget = m_pImpl->hwnd;
        if (::RegisterRawInputDevices(&rid, 1, sizeof(rid)) == FALSE)
        {
            NS_LOG_ERROR(
                Platform, "RegisterRawInputDevices 失敗、 相対マウスは無効 (GetLastError={})", ::GetLastError());
        }

        int showCommand = SW_HIDE;
        if (desc.visible)
            showCommand = SW_SHOW;
        ::ShowWindow(m_pImpl->hwnd, showCommand);
        ::UpdateWindow(m_pImpl->hwnd);
    }

    Window::~Window()
    {
        if (m_pImpl->hwnd != nullptr)
        {
            ::DestroyWindow(m_pImpl->hwnd);
            m_pImpl->hwnd = nullptr;
        }
        if (m_pImpl->classAtom != 0)
        {
            ::UnregisterClassW(m_pImpl->className.c_str(), m_pImpl->hInstance);
            m_pImpl->classAtom = 0;
        }
        s_instance = nullptr;
    }

    bool Window::IsValid() const noexcept
    {
        return m_pImpl->hwnd != nullptr;
    }

    void Window::PollMessages() noexcept
    {
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                m_pImpl->shouldClose = true;
                continue;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    bool Window::ShouldClose() const noexcept
    {
        return m_pImpl->shouldClose;
    }

    ::NS::Math::Size2D Window::Size() const noexcept
    {
        return m_pImpl->size;
    }

    void* Window::NativeHandle() const noexcept
    {
        return static_cast<void*>(m_pImpl->hwnd);
    }

    void Window::SetTitle(std::string_view utf8Title) noexcept
    {
        if (m_pImpl->hwnd == nullptr)
        {
            return;
        }
        const std::wstring wide = ::NS::Core::StringUtils::WideFromUtf8(utf8Title);
        ::SetWindowTextW(m_pImpl->hwnd, wide.c_str());
    }

    void Window::SetCursorVisible(bool visible) noexcept
    {
        m_pImpl->cursorVisible = visible;
        // 次の WM_SETCURSOR を待たず即時反映する。 マウスが動かなくても切替わる
        HCURSOR cursor = nullptr;
        if (visible)
            cursor = ::LoadCursorW(nullptr, IDC_ARROW);
        ::SetCursor(cursor);
    }

    bool Window::IsCursorVisible() const noexcept
    {
        return m_pImpl->cursorVisible;
    }

    void Window::RequestClose() noexcept
    {
        if (m_pImpl->hwnd != nullptr)
        {
            ::PostMessageW(m_pImpl->hwnd, WM_CLOSE, 0, 0);
        }
    }

    void Window::SetResizeCallback(std::function<void(::NS::Math::Size2D)> cb)
    {
        m_pImpl->onResize = std::move(cb);
    }

    void Window::SetCloseCallback(std::function<void()> cb)
    {
        m_pImpl->onClose = std::move(cb);
    }

    void Window::AttachInput(Input* input) noexcept
    {
        m_pImpl->input = input;
    }

    void Window::SetMessageHook(MessageHook hook)
    {
        m_pImpl->messageHook = std::move(hook);
    }

} // namespace NS::Platform
