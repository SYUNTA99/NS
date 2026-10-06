#pragma once

#include "NSlib/Windows/Keyboard.h"

#include <cstdint>

namespace NS::OS
{

    class Input;

    //! WndProc から呼ばれる Win32 入力メッセージディスパッチ。引数は WPARAM/LPARAM 互換
    void DispatchWin32MessageToInput(Input& input,
                                     unsigned int msg,
                                     std::uintptr_t wparam,
                                     std::intptr_t lparam) noexcept;

} // namespace NS::OS
