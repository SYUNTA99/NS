#pragma once

#include "NSlib/Core/Types.h"

#include <array>
#include <cstddef>

namespace NS::OS
{
        //! 物理キー識別子
        enum class Key : int
        {
            Unknown = 0,

            A,
            B,
            C,
            D,
            E,
            F,
            G,
            H,
            I,
            J,
            K,
            L,
            M,
            N,
            O,
            P,
            Q,
            R,
            S,
            T,
            U,
            V,
            W,
            X,
            Y,
            Z,

            Num0,
            Num1,
            Num2,
            Num3,
            Num4,
            Num5,
            Num6,
            Num7,
            Num8,
            Num9,

            Left,
            Right,
            Up,
            Down,

            Space,
            Enter,
            Escape,
            Tab,
            Backspace,
            Delete,
            Shift,
            Ctrl,
            Alt,

            F1,
            F2,
            F3,
            F4,
            F5,
            F6,
            F7,
            F8,
            F9,
            F10,
            F11,
            F12,

            Count
        };

        //! 単一スレッドで読むキー状態。押している間と、押した回数を持つ
        class Keyboard
        {
        public:
            [[nodiscard]] bool IsHeld(Key k) const noexcept;

            //! 押した瞬間ごとに 1 足した回数。下ろさない。押した瞬間は InputReader で読む
            [[nodiscard]] uint32 PressCount(Key k) const noexcept;

            //! @brief OSのキー押下イベントを受け取る内部API
            //! @details 押していないキーだけを数える。押したままの OS の繰り返しは数えない
            //! @note Key::Unknown は処理されず無視される
            void OnKeyDown(Key k) noexcept;

            //! @brief OSのキー解放イベントを受け取る内部API
            //! @note Key::Unknown は処理されず無視される
            void OnKeyUp(Key k) noexcept;

            //! フォーカス消失時などの押しっぱなしを防ぐため、押している間を解除する。押した回数は下ろさない
            void ClearState() noexcept;

        private:
            static constexpr std::size_t k_KeyCount = static_cast<std::size_t>(Key::Count);

            std::array<bool, k_KeyCount> m_current{};
            std::array<uint32, k_KeyCount> m_presses{};
        };
} // namespace NS::OS
