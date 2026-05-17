#include <ns/platform/window.h>

#include <ns/core/log_categories.h>
#include <ns/core/logger.h>
#include <ns/core/string_utils.h>
#include <ns/platform/detail/win32_window.h>

namespace ns::platform
{

    namespace
    {
        /// シングルトン前提。ctor で 2 重生成検知に使い、WndProc から this を引く。
        Window::Impl* s_instance = nullptr;

        LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
        {
            Window::Impl* impl = s_instance;
            if (impl == nullptr)
            {
                return ::DefWindowProcW(hwnd, msg, wparam, lparam);
            }

            switch (msg)
            {
            case WM_SIZE:
            {
                if (wparam == SIZE_MINIMIZED)
                {
                    break;
                }
                impl->width = LOWORD(lparam);
                impl->height = HIWORD(lparam);
                if (impl->onResize)
                {
                    impl->onResize(impl->width, impl->height);
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
            NS_LOG_FATAL(::ns::core::LogCat::Platform, "Window は単一インスタンス前提です (二重生成)");
        }
        s_instance = m_pImpl.get();

        ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        m_pImpl->hInstance = ::GetModuleHandleW(nullptr);
        m_pImpl->width = desc.width;
        m_pImpl->height = desc.height;
        m_pImpl->className = L"NS_Window";

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = m_pImpl->hInstance;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = m_pImpl->className.c_str();

        m_pImpl->classAtom = ::RegisterClassExW(&wc);
        if (m_pImpl->classAtom == 0)
        {
            NS_LOG_ERROR(::ns::core::LogCat::Platform, "RegisterClassExW failed (GetLastError={})", ::GetLastError());
            s_instance = nullptr;
            return;
        }

        RECT rect{0, 0, desc.width, desc.height};
        const DWORD style = WS_OVERLAPPEDWINDOW;
        const DWORD exStyle = 0;
        ::AdjustWindowRectEx(&rect, style, FALSE, exStyle);

        const std::wstring wideTitle = ::ns::core::StringUtils::WideFromUtf8(desc.title);

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
            NS_LOG_ERROR(::ns::core::LogCat::Platform, "CreateWindowExW failed (GetLastError={})", ::GetLastError());
            ::UnregisterClassW(m_pImpl->className.c_str(), m_pImpl->hInstance);
            m_pImpl->classAtom = 0;
            s_instance = nullptr;
            return;
        }

        ::ShowWindow(m_pImpl->hwnd, SW_SHOW);
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

    void Window::PollMessages()
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

    int Window::Width() const noexcept
    {
        return m_pImpl->width;
    }

    int Window::Height() const noexcept
    {
        return m_pImpl->height;
    }

    void* Window::NativeHandle() const noexcept
    {
        return static_cast<void*>(m_pImpl->hwnd);
    }

    void Window::SetTitle(std::string_view utf8Title)
    {
        if (m_pImpl->hwnd == nullptr)
        {
            return;
        }
        const std::wstring wide = ::ns::core::StringUtils::WideFromUtf8(utf8Title);
        ::SetWindowTextW(m_pImpl->hwnd, wide.c_str());
    }

    void Window::RequestClose() noexcept
    {
        ::PostQuitMessage(0);
    }

    void Window::SetResizeCallback(std::function<void(int, int)> cb)
    {
        m_pImpl->onResize = std::move(cb);
    }

    void Window::SetCloseCallback(std::function<void()> cb)
    {
        m_pImpl->onClose = std::move(cb);
    }

} // namespace ns::platform
