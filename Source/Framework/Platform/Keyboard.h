#pragma once

#include <array>
#include <cstddef>

namespace NS::Platform
{

    /// 物理キー識別子。VK コードや特定 OS の値とは独立して定義する。
    /// 公開ヘッダから <windows.h> を排除するための間接層。
    /// kCount は配列サイズ用の番兵、Unknown は未マップ VK の戻り値。
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

    /// キーボード入力の現在/前フレーム状態を保持する。
    /// `Update()` をフレーム頭で 1 回呼び、`OnKeyDown` / `OnKeyUp` は WndProc 経由で呼ばれる。
    /// マルチスレッドは未サポート (単一スレッド前提)。
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

        /// previous = current のコピー。次フレーム用の差分判定基準を更新する。
        void Update() noexcept;

        /// WndProc から呼ばれる内部 API。Key::Unknown は無視する。
        void OnKeyDown(Key k) noexcept;

        /// WndProc から呼ばれる内部 API。Key::Unknown は無視する。
        void OnKeyUp(Key k) noexcept;

        /// 現在状態を全て false にクリアする (WM_KILLFOCUS で stuck key 防止)。
        void ClearState() noexcept;

    private:
        static constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::kCount);

        std::array<bool, kKeyCount> m_current{};
        std::array<bool, kKeyCount> m_previous{};
    };

} // namespace NS::Platform
