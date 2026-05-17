#pragma once

#include <ns/platform/window.h>

#include <windows.h>

#include <functional>
#include <string>

namespace ns::platform
{

    /// Window の内部状態。win32 依存のシンボルはこのヘッダ以下にのみ存在する。
    struct Window::Impl
    {
        HWND hwnd = nullptr;
        ATOM classAtom = 0;
        HINSTANCE hInstance = nullptr;

        int width = 0;
        int height = 0;
        bool shouldClose = false;

        std::wstring className;

        std::function<void(int, int)> onResize;
        std::function<void()> onClose;
    };

} // namespace ns::platform
