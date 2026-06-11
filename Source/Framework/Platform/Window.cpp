#include <Framework/Platform/Window.h>

#include <Framework/Core/LogCategories.h>
#include <Framework/Core/Logger.h>
#include <Framework/Core/StringUtils.h>
#include <Framework/Platform/Input.h>
#include <Framework/Platform/detail/input_win32.h>
#include <Framework/Platform/detail/win32_window.h>
#include <Framework/UI/ImGuiContext.h>

namespace NS::Platform
{

    namespace
    {
        /// シングルトン前提。コンストラクタで 2 重生成検知に使い、WndProc から this を引く
        Window::Impl* s_instance = nullptr;

        /// Input に転送する Win32 メッセージ判定。WndProc switch の case ラベル列を集約
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

        /// キーボード系メッセージ (WantCaptureKeyboard でゲートする対象)
        /// WM_KILLFOCUS は ImGui キャプチャに関係なく Input を flush するので除外
        [[nodiscard]] constexpr bool IsKeyboardMessage(UINT msg) noexcept
        {
            return msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP;
        }

        /// マウス系メッセージ (WantCaptureMouse でゲートする対象)
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

            // ImGui に先に転送する。ゲーム入力との競合は WantCaptureMouse / WantCaptureKeyboard でゲートする
            if (impl->imgui != nullptr)
            {
                (void)impl->imgui->ForwardWndProc(static_cast<void*>(hwnd),
                                                  static_cast<std::uint32_t>(msg),
                                                  static_cast<std::uintptr_t>(wparam),
                                                  static_cast<std::intptr_t>(lparam));
            }

            if (IsInputMessage(msg))
            {
                // ImGui がキャプチャ中はゲーム側へイベントを流さない
                if (impl->imgui != nullptr)
                {
                    if ((IsKeyboardMessage(msg) && impl->imgui->WantCaptureKeyboard()) ||
                        (IsMouseMessage(msg) && impl->imgui->WantCaptureMouse()))
                    {
                        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
                    }
                }

                if (impl->input != nullptr)
                {
                    DispatchWin32MessageToInput(*impl->input,
                                                static_cast<unsigned int>(msg),
                                                static_cast<std::uintptr_t>(wparam),
                                                static_cast<std::intptr_t>(lparam));
                }
                return ::DefWindowProcW(hwnd, msg, wparam, lparam);
            }

            switch (msg)
            {
            case WM_ERASEBKGND:
            {
                // D3D が毎フレーム Present するため GDI 背景消去は不要。DefWindowProc に流すと初回 Present
                // 前に白フラッシュが出る
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
            NS_LOG_FATAL(::NS::Core::LogCat::Platform, "Window は単一インスタンス前提です (二重生成)");
        }
        s_instance = m_pImpl.get();

        ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        m_pImpl->hInstance = ::GetModuleHandleW(nullptr);
        m_pImpl->size = desc.size;
        m_pImpl->className = L"NS_Window";

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = m_pImpl->hInstance;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        // hbrBackground=nullptr + WM_ERASEBKGND 抑止の両方が揃って白フラッシュが消える
        wc.hbrBackground = nullptr;
        wc.lpszClassName = m_pImpl->className.c_str();

        m_pImpl->classAtom = ::RegisterClassExW(&wc);
        if (m_pImpl->classAtom == 0)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Platform, "RegisterClassExW failed (GetLastError={})", ::GetLastError());
            s_instance = nullptr;
            return;
        }

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
            NS_LOG_ERROR(::NS::Core::LogCat::Platform, "CreateWindowExW failed (GetLastError={})", ::GetLastError());
            ::UnregisterClassW(m_pImpl->className.c_str(), m_pImpl->hInstance);
            m_pImpl->classAtom = 0;
            s_instance = nullptr;
            return;
        }

        ::ShowWindow(m_pImpl->hwnd, desc.visible ? SW_SHOW : SW_HIDE);
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

    void Window::AttachImGui(NS::UI::ImGuiContext* imgui) noexcept
    {
        m_pImpl->imgui = imgui;
    }

} // namespace NS::Platform
