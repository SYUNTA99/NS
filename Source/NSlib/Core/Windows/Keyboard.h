#pragma once

#include "NSlib/Core/Windows/ButtonStates.h"

namespace NS
{
    namespace OS
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

        // ButtonStates の先頭を Key::A にして、Key::Unknown だけを無視する
        static_assert(static_cast<int>(Key::A) == static_cast<int>(Key::Unknown) + 1);

        //! 単一スレッドで読むキー状態。押している間と、押した回数を持つ
        class Keyboard : public ButtonStates<Key, Key::A>
        {
        public:
            //! OSのキー押下イベントを受け取る内部API
            //! 押していないキーだけを数える。押したままの OS の繰り返しは数えない
            //! @note Key::Unknown は処理されず無視される
            void OnKeyDown(Key k) noexcept { Press(k); }

            //! OSのキー解放イベントを受け取る内部API
            //! @note Key::Unknown は処理されず無視される
            void OnKeyUp(Key k) noexcept { Release(k); }

            //! フォーカス消失時などの押しっぱなしを防ぐため、押している間を解除する。押した回数は下ろさない
            void ClearState() noexcept { ReleaseAll(); }
        };
    } // namespace OS
} // namespace NS
