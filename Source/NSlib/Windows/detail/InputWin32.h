#pragma once

#include "NSlib/Windows/Input.h"

#include <windows.h>

namespace NS
{
    namespace OS
    {
        // OS の入力を手元の機器へ書く。中立の間も書き先は替わらない
        void DispatchWin32MessageToInput(Input& input, UINT msg, WPARAM wparam, LPARAM lparam) noexcept;

        [[nodiscard]] constexpr bool IsKeyboardMessage(UINT msg) noexcept
        {
            return msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP;
        }

        // 生のマウス移動 WM_INPUT も含め、マウスから届く入力か
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
            case WM_INPUT:
                return true;
            default:
                return false;
            }
        }

        // UI が取っていてゲームへ流さない入力か
        // 左ボタンの押下は、UI がマウスを持つ間も左の受け渡しが登録されていれば流す
        [[nodiscard]] inline bool IsTakenByUi(const Input& input, UINT msg) noexcept
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
    } // namespace OS
} // namespace NS
