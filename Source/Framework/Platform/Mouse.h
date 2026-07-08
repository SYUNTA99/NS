#pragma once

/// @file Mouse.h
/// @brief NS::Platform::Mouse / MouseButton — マウス入力の状態保持
///
/// @details 座標はウィンドウのクライアント領域で左上原点、 ホイールは
/// 120 にあたる `WHEEL_DELTA` 単位の縦スクロール。 `OnMove` / `OnButton*` / `OnWheel`
/// は WndProc 経由で呼ばれる。 `Update()` は fixed step ループの頭で 1 回呼び、
/// previous 状態の退避とホイールのリセットを行う。 マルチスレッドは未サポート

#include <array>
#include <cstddef>

namespace NS::Platform
{

    /// マウスボタン識別子。Count は配列サイズ用番兵
    /// X1 / X2 は OS 慣習に従う5 ボタンマウスのサイドボタン
    enum class MouseButton : int
    {
        Left = 0,
        Right,
        Middle,
        X1,
        X2,

        Count
    };

    /// マウス入力の現在/前フレーム状態を保持する。フレーム頭で Update() を 1 回呼ぶこと
    class Mouse
    {
    public:
        Mouse() = default;

        [[nodiscard]] bool IsPressed(MouseButton b) const noexcept;
        [[nodiscard]] bool IsHeld(MouseButton b) const noexcept;
        [[nodiscard]] bool IsReleased(MouseButton b) const noexcept;

        /// クライアント座標、左上原点
        [[nodiscard]] int GetX() const noexcept { return m_x; }
        [[nodiscard]] int GetY() const noexcept { return m_y; }

        /// 前フレーム位置からの差分。 相対モード中は OnRawMove で積んだ物理移動量を返す
        [[nodiscard]] int GetDeltaX() const noexcept
        {
            if (m_relativeMode)
                return m_rawDeltaX;
            return m_x - m_prevX;
        }
        [[nodiscard]] int GetDeltaY() const noexcept
        {
            if (m_relativeMode)
                return m_rawDeltaY;
            return m_y - m_prevY;
        }

        /// WHEEL_DELTA=120 単位の縦ホイールデルタ。Update() で 0 リセット
        [[nodiscard]] int GetWheelDelta() const noexcept { return m_wheel; }

        /// previous = current をコピーし wheel を 0 リセット。差分判定基準を更新する
        void Update() noexcept;

        /// WndProc から呼ばれる内部 API
        void OnMove(int x, int y) noexcept;
        void OnButtonDown(MouseButton b) noexcept;
        void OnButtonUp(MouseButton b) noexcept;
        void OnWheel(int delta) noexcept;

        /// 相対マウスモードの切替。 プレイ中にカーソルを消して視点操作する時 true にする
        /// true の間 GetDeltaX/Y はカーソル位置差でなく OnRawMove で積んだ物理移動量を返すため
        /// カーソルが窓の端に張り付いても視点が止まらない
        void SetRelativeMode(bool enabled) noexcept;
        [[nodiscard]] bool IsRelativeMode() const noexcept { return m_relativeMode; }

        /// Raw Input の相対移動量を積む。 WndProc の WM_INPUT 経由で呼ばれる。 Update() で 0 リセット
        void OnRawMove(int dx, int dy) noexcept;

        /// フォーカス喪失時に呼ぶ。ボタン状態とホイールをクリアし位置は維持する
        void ClearState() noexcept;

    private:
        static constexpr std::size_t kButtonCount = static_cast<std::size_t>(MouseButton::Count);

        std::array<bool, kButtonCount> m_current{};
        std::array<bool, kButtonCount> m_previous{};

        int m_x = 0;
        int m_y = 0;
        int m_prevX = 0;
        int m_prevY = 0;
        int m_wheel = 0;
        int m_rawDeltaX = 0;
        int m_rawDeltaY = 0;
        bool m_relativeMode = false;
    };

} // namespace NS::Platform
