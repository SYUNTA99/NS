#pragma once

#include "Framework/Platform/Keyboard.h"

#include <cstdint>

namespace NS::Platform
{

    class Input;

    /// 有効範囲 1〜254 の Win32 VK コードを Key enum に変換する。未マップ / 範囲外は Key::Unknown
    /// <windows.h> 依存を避けるため引数は unsigned int で受ける。実体は WPARAM 互換
    [[nodiscard]] Key MapVkToKey(unsigned int vk) noexcept;

    /// WndProc から呼ばれる Win32 入力メッセージディスパッチ。引数は WPARAM/LPARAM 互換
    void DispatchWin32MessageToInput(Input& input,
                                     unsigned int msg,
                                     std::uintptr_t wparam,
                                     std::intptr_t lparam) noexcept;

} // namespace NS::Platform
