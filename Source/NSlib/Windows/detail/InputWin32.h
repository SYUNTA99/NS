#pragma once

#include "NSlib/Windows/Keyboard.h"

#include <windows.h>

namespace NS
{
    namespace OS
    {
        class Input;

        void DispatchWin32MessageToInput(Input& input, UINT msg, WPARAM wparam, LPARAM lparam) noexcept;
    } // namespace OS
} // namespace NS
