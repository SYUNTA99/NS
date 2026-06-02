#pragma once

/// @file Gamepad.h
/// @brief NS::Platform::Gamepad / GamepadButton / Stick — XInput 互換ゲームパッド
///
/// @details `Update()` は fixed step ループの頭で 1 回呼び、 内部で
/// `XInputGetState()` をポーリングする。 `ERROR_DEVICE_NOT_CONNECTED` の場合は
/// `IsConnected() == false` で復帰し、 ボタン / 軸はリセットされる
/// (ボタン固着防止)。 スティック / トリガーはデッドゾーン適用後の値を返す
/// マルチスレッドは未サポート (単一スレッド前提)

#include <array>
#include <cstddef>

namespace NS::Platform
{

    /// XInput 互換コントローラのボタン識別子
    /// kCount は配列サイズ用の番兵
    enum class GamepadButton : int
    {
        A = 0,
        B,
        X,
        Y,

        LeftShoulder,
        RightShoulder,

        Back,
        Start,

        LeftThumb,
        RightThumb,

        DPadUp,
        DPadDown,
        DPadLeft,
        DPadRight,

        kCount
    };

    /// アナログスティックの正規化済 (x, y) ペア
    /// 各軸は -1.0〜1.0、デッドゾーン内は 0.0 にクランプ済
    /// Y 軸は XInput 生値の符号を維持 (上が正)
    struct Stick
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    /// XInput ベースのゲームパッド状態
    /// `Update()` をフレーム頭で 1 回呼ぶ。内部で `XInputGetState()` をポーリングし、
    /// `ERROR_DEVICE_NOT_CONNECTED` の場合は `IsConnected() = false`、ボタン/軸はリセット
    /// マルチスレッドは未サポート (単一スレッド前提)
    class Gamepad
    {
    public:
        Gamepad() = default;
        explicit Gamepad(int userIndex) noexcept;

        /// XInputGetState 戻り値が ERROR_SUCCESS なら true
        [[nodiscard]] bool IsConnected() const noexcept;

        /// このフレームで押された (前 false → 現 true)
        [[nodiscard]] bool IsPressed(GamepadButton b) const noexcept;

        /// 押し続けている (現 true)
        [[nodiscard]] bool IsHeld(GamepadButton b) const noexcept;

        /// このフレームで離した (前 true → 現 false)
        [[nodiscard]] bool IsReleased(GamepadButton b) const noexcept;

        /// 左スティック (-1.0〜1.0)、デッドゾーン適用済
        [[nodiscard]] Stick LeftStick() const noexcept;

        /// 右スティック (-1.0〜1.0)、デッドゾーン適用済
        [[nodiscard]] Stick RightStick() const noexcept;

        /// 左トリガー (0.0〜1.0)、トリガースレッショルド未満は 0.0
        [[nodiscard]] float LeftTrigger() const noexcept;

        /// 右トリガー (0.0〜1.0)、トリガースレッショルド未満は 0.0
        [[nodiscard]] float RightTrigger() const noexcept;

        /// previous = current 退避後、XInputGetState() でポーリング
        /// 未接続時は m_current をリセット (ボタン固着防止)
        void Update() noexcept;

    private:
        static constexpr std::size_t kButtonCount = static_cast<std::size_t>(GamepadButton::kCount);

        int m_userIndex = 0;
        bool m_connected = false;
        std::array<bool, kButtonCount> m_current{};
        std::array<bool, kButtonCount> m_previous{};
        Stick m_leftStick{};
        Stick m_rightStick{};
        float m_leftTrigger = 0.0f;
        float m_rightTrigger = 0.0f;
    };

} // namespace NS::Platform
