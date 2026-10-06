#pragma once

#include "NSlib/Windows/Window.h"

#include <windows.h>

#include <functional>
#include <optional>

namespace NS::OS
{

    //! @brief Window の内部状態。win32 依存のシンボルはこのヘッダ以下にのみ存在する
    struct Window::Impl
    {
        HWND hwnd = nullptr;
        ATOM classAtom = 0; // RegisterClassExW の戻り
        HINSTANCE hInstance = nullptr;

        ::NS::Size2D size{0, 0};
        bool shouldClose = false; // WM_QUIT を受けたか

        std::function<void(::NS::Size2D)> onResize;
        std::function<void()> onClose;

        Input* input = nullptr; // 入力転送先 (非所有)
        Window::MessageHook messageHook;

        // false の間はクライアント領域のカーソルを消す。WM_SETCURSOR がこの値を見て適用する
        bool cursorVisible = true;

        bool cursorLocked = false;
        // 一度も前に出ない窓には WM_SETFOCUS が来ない。作成時に実際の状態を書く
        bool hasFocus = false;
        std::optional<POINT> lockPoint; // 無い時はクライアント領域の中央
    };

} // namespace NS::OS
