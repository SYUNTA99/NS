#pragma once

#include <Framework/Platform/Window.h>

#include "Framework/Framework.h"

#include <functional>
#include <string>

namespace NS::Platform
{

    /// Window の内部状態。win32 依存のシンボルはこのヘッダ以下にのみ存在する。
    struct Window::Impl
    {
        HWND hwnd = nullptr;
        ATOM classAtom = 0;
        HINSTANCE hInstance = nullptr;

        ::NS::Core::Size2D size{0, 0};
        bool shouldClose = false;

        std::wstring className;

        std::function<void(::NS::Core::Size2D)> onResize;
        std::function<void()> onClose;

        Input* input = nullptr;
    };

} // namespace NS::Platform
