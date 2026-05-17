#pragma once

#include <ns/platform/keyboard.h>

#include <cstdint>

namespace ns::platform
{

    class Input;

    /// Win32 VK コード (0〜255) を Key enum に変換する。未マップは Key::Unknown。
    /// <windows.h> 依存を避けるため引数は unsigned int で受ける (VK は WPARAM サイズ)。
    [[nodiscard]] Key MapVkToKey(unsigned int vk) noexcept;

    /// Window WndProc から呼ばれる Win32 メッセージディスパッチ。
    /// WM_KEYDOWN / WM_KEYUP / WM_SYSKEYDOWN / WM_SYSKEYUP / WM_KILLFOCUS のみ処理。
    /// 引数は <windows.h> 露出回避のため intptr 系で受ける (実体は WPARAM/LPARAM)。
    void DispatchWin32MessageToInput(Input& input,
                                     unsigned int msg,
                                     std::uintptr_t wparam,
                                     std::intptr_t lparam) noexcept;

} // namespace ns::platform
