#pragma once

/// @file Keyboard.h
/// @brief NS::Platform::Keyboard / Key — 物理キー入力の状態保持
///
/// @details `Key` enum は VK コード非依存の独立識別子で公開ヘッダから
/// `<windows.h>` を排除する間接層。 `OnKeyDown` / `OnKeyUp` は WndProc 経由で
/// 呼ばれ、 `Update()` は fixed step ループの頭で 1 回呼ぶ (前フレームとの
/// edge 判定基準を更新)。 マルチスレッドは未サポート (単一スレッド前提)

#include <array>
#include <cstddef>

namespace NS::Platform
{

    /// 物理キー識別子。VK コード非依存の間接層。kCount は配列サイズ用番兵
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

        kCount
    };

    /// キーボード入力の現在/前フレーム状態を保持する。フレーム頭で Update() を 1 回呼ぶこと
    class Keyboard
    {
    public:
        Keyboard() = default;

        /// このフレームで押された (前 false → 現 true)
        [[nodiscard]] bool IsPressed(Key k) const noexcept;

        /// 押し続けている (現 true)
        [[nodiscard]] bool IsHeld(Key k) const noexcept;

        /// このフレームで離した (前 true → 現 false)
        [[nodiscard]] bool IsReleased(Key k) const noexcept;

        /// previous = current をコピーし、差分判定基準を更新する
        void Update() noexcept;

        /// WndProc から呼ばれる内部 API。Key::Unknown は無視する
        void OnKeyDown(Key k) noexcept;

        /// WndProc から呼ばれる内部 API。Key::Unknown は無視する
        void OnKeyUp(Key k) noexcept;

        /// 現在状態を全て false にクリアする (WM_KILLFOCUS でキー固着防止)
        void ClearState() noexcept;

    private:
        static constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::kCount);

        std::array<bool, kKeyCount> m_current{};
        std::array<bool, kKeyCount> m_previous{};
    };

} // namespace NS::Platform
