#pragma once

#include <array>
#include <cstddef>

namespace NS::Platform
{

    /// 物理キー識別子
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

    //! @brief キーボード入力の現在および前フレームの状態を追跡・管理するクラス。
    //! @note スレッドセーフではないため、単一スレッドからのアクセスを前提とする。
    class Keyboard
    {
    public:
        Keyboard() = default;

        //! @brief 対象のキーが現在のフレームで新たに押されたかどうかを判定する
        [[nodiscard]] bool IsPressed(Key k) const noexcept;

        //! @brief 対象のキーが現在押され続けているかどうかを判定する
        [[nodiscard]] bool IsHeld(Key k) const noexcept;

        //! @brief 対象のキーが現在のフレームで離されたかどうかを判定する
        [[nodiscard]] bool IsReleased(Key k) const noexcept;

        //! @brief 入力状態のフレーム境界を更新し、差分判定の基準を進める
        void Update() noexcept;

        //! @brief OSのキー押下イベントを受け取る内部API
        //! @note Key::Unknown は処理されず無視される
        void OnKeyDown(Key k) noexcept;

        //! @brief OSのキー解放イベントを受け取る内部API
        //! @note Key::Unknown は処理されず無視される
        void OnKeyUp(Key k) noexcept;

        //! @brief すべてのキー入力状態を強制的にオフ（解放状態）にリセットする
        //! @note ウィンドウのフォーカス消失時など、キーが押されたまま内部で固着するバグを防ぐために利用する
        void ClearState() noexcept;

    private:
        static constexpr std::size_t k_KeyCount = static_cast<std::size_t>(Key::Count);

        std::array<bool, k_KeyCount> m_current{};
        std::array<bool, k_KeyCount> m_previous{};
    };

} // namespace NS::Platform
