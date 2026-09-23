#pragma once

#include "Runtime/Platform/Window.h"

#include <windows.h>

#include <functional>
#include <string>

namespace NS::Platform
{

    //! @brief Window の内部状態。win32 依存のシンボルはこのヘッダ以下にのみ存在する
    struct Window::Impl
    {
        HWND hwnd = nullptr;
        ATOM classAtom = 0; // RegisterClassExW の戻り
        HINSTANCE hInstance = nullptr;

        ::NS::Core::Size2D size{0, 0};
        bool shouldClose = false; // WM_QUIT を受けたか

        std::wstring className;

        std::function<void(::NS::Core::Size2D)> onResize;
        std::function<void()> onClose;

        Input* input = nullptr; // 入力転送先 (非所有)
        Window::MessageHook messageHook;

        // false の間はクライアント領域のカーソルを消す。WM_SETCURSOR がこの値を見て適用する
        bool cursorVisible = true;

        bool cursorLocked = false;
        // 一度も前に出ない窓には WM_SETFOCUS が来ない。作成時に実際の状態を書く
        bool hasFocus = false;
        bool lockPointSet = false;
        int lockPointX = 0;
        int lockPointY = 0;
    };

} // namespace NS::Platform
