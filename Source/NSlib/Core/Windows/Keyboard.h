#pragma once

#include <array>
#include <cstddef>

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

        //! 単一スレッドで読むキー状態。Update を境界に押下・解放の差分を判定する
        class Keyboard
        {
        public:
            //! 直前の Update 後に押されたか
            [[nodiscard]] bool IsPressed(Key k) const noexcept;

            [[nodiscard]] bool IsHeld(Key k) const noexcept;

            //! 直前の Update 後に離されたか
            [[nodiscard]] bool IsReleased(Key k) const noexcept;

            //! @brief 入力状態のフレーム境界を更新し、差分判定の基準を進める
            void Update() noexcept;

            //! @brief OSのキー押下イベントを受け取る内部API
            //! @note Key::Unknown は処理されず無視される
            void OnKeyDown(Key k) noexcept;

            //! @brief OSのキー解放イベントを受け取る内部API
            //! @note Key::Unknown は処理されず無視される
            void OnKeyUp(Key k) noexcept;

            //! フォーカス消失時などの押しっぱなしを防ぐため、入力を解除する
            void ClearState() noexcept;

        private:
            static constexpr std::size_t k_KeyCount = static_cast<std::size_t>(Key::Count);

            std::array<bool, k_KeyCount> m_current{};
            std::array<bool, k_KeyCount> m_previous{};
        };
    } // namespace OS
} // namespace NS
