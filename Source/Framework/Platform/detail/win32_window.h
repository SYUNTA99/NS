#pragma once

#include "Framework/Platform/Window.h"

#include <windows.h>

#include <functional>
#include <string>

namespace NS::Platform
{

    /// Window の内部状態。win32 依存のシンボルはこのヘッダ以下にのみ存在する
    struct Window::Impl
    {
        HWND hwnd = nullptr;
        ATOM classAtom = 0;
        HINSTANCE hInstance = nullptr;

        ::NS::Math::Size2D size{0, 0};
        bool shouldClose = false;

        std::wstring className;

        std::function<void(::NS::Math::Size2D)> onResize;
        std::function<void()> onClose;

        Input* input = nullptr;
        Window::MessageHook messageHook;

        // false の間はクライアント領域のカーソルを消す。 WM_SETCURSOR が毎フレームこれを見て適用する
        bool cursorVisible = true;
    };

} // namespace NS::Platform
