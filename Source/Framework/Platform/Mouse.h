#pragma once

/// @file Mouse.h
/// @brief NS::Platform::Mouse / MouseButton — マウス入力の状態保持。
///
/// @details 座標はウィンドウのクライアント領域 (左上原点)、 ホイールは
/// `WHEEL_DELTA` (=120) 単位の縦スクロール。 `OnMove` / `OnButton*` / `OnWheel`
/// は WndProc 経由で呼ばれる。 `Update()` は fixed step ループの頭で 1 回呼び、
/// previous 状態の退避とホイールのリセットを行う。 マルチスレッドは未サポート。

#include <array>
#include <cstddef>

namespace NS::Platform
{

    /// マウスボタン識別子。kCount は配列サイズ用番兵。
    /// X1 / X2 は OS 慣習に従う5 ボタンマウスのサイドボタン。
    enum class MouseButton : int
    {
        Left = 0,
        Right,
        Middle,
        X1,
        X2,

        kCount
    };

    /// マウス入力の現在/前フレーム状態を保持する。
    /// `Update()` をフレーム頭で 1 回呼び、`OnMove` / `OnButton*` / `OnWheel` は WndProc 経由で呼ばれる。
    /// 座標はウィンドウのクライアント領域、ホイールは WHEEL_DELTA (=120) 単位の縦スクロール。
    class Mouse
    {
    public:
        Mouse() = default;

        [[nodiscard]] bool IsPressed(MouseButton b) const noexcept;
        [[nodiscard]] bool IsHeld(MouseButton b) const noexcept;
        [[nodiscard]] bool IsReleased(MouseButton b) const noexcept;

        /// クライアント座標、左上原点
        [[nodiscard]] int X() const noexcept { return m_x; }
        [[nodiscard]] int Y() const noexcept { return m_y; }

        /// 前フレーム位置からの差分
        [[nodiscard]] int DeltaX() const noexcept { return m_x - m_prevX; }
        [[nodiscard]] int DeltaY() const noexcept { return m_y - m_prevY; }

        /// 縦ホイールデルタ。WHEEL_DELTA (=120) 単位、正=奥/上、負=手前/下。
        /// Update() で 0 リセット。
        [[nodiscard]] int WheelDelta() const noexcept { return m_wheel; }

        /// previous = current のコピー (ボタン状態 + 位置 x/y) + wheel = 0 リセット。
        /// フレーム頭で 1 回呼ぶ。次フレームでの差分判定 (Pressed/Released/Delta*) の基準を更新する。
        void Update() noexcept;

        /// WndProc から呼ばれる内部 API。
        void OnMove(int x, int y) noexcept;
        void OnButtonDown(MouseButton b) noexcept;
        void OnButtonUp(MouseButton b) noexcept;
        void OnWheel(int delta) noexcept;

        /// フォーカス喪失時に呼ぶ。ボタン状態とホイールをクリア (位置は維持)。
        void ClearState() noexcept;

    private:
        static constexpr std::size_t kButtonCount = static_cast<std::size_t>(MouseButton::kCount);

        std::array<bool, kButtonCount> m_current{};
        std::array<bool, kButtonCount> m_previous{};

        int m_x = 0;
        int m_y = 0;
        int m_prevX = 0;
        int m_prevY = 0;
        int m_wheel = 0;
    };

} // namespace NS::Platform
