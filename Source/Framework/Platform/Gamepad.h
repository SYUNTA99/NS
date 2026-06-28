#pragma once

/// @file Gamepad.h
/// @brief NS::Platform::Gamepad / GamepadButton / Stick — XInput 互換ゲームパッド
///
/// @details `Update()` は fixed step ループの頭で 1 回呼び、 内部で
/// `XInputGetState()` をポーリングする。 `ERROR_DEVICE_NOT_CONNECTED` の場合は
/// `IsConnected() == false` で復帰し、 ボタン / 軸はボタン固着防止のため
/// リセットされる。 スティック / トリガーはデッドゾーン適用後の値を返す
/// 単一スレッド前提でマルチスレッドは未サポート

#include <array>
#include <cstddef>

namespace NS::Platform
{

    /// XInput 互換コントローラのボタン識別子。Count は配列サイズ用番兵
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

        Count
    };

    /// x と y を正規化したアナログスティック値。各軸 -1.0〜1.0、デッドゾーン内は 0.0
    struct Stick
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    /// XInput ベースのゲームパッド状態。フレーム頭で Update() を 1 回呼ぶこと
    /// 未接続時は IsConnected() = false でボタン/軸はリセットし固着を防ぐ
    class Gamepad
    {
    public:
        Gamepad() = default;
        explicit Gamepad(int userIndex) noexcept;

        /// XInputGetState 戻り値が ERROR_SUCCESS なら true
        [[nodiscard]] bool IsConnected() const noexcept;

        /// このフレームで押された。前フレーム false から現フレーム true への変化
        [[nodiscard]] bool IsPressed(GamepadButton b) const noexcept;

        /// 押し続けている。現フレームが true
        [[nodiscard]] bool IsHeld(GamepadButton b) const noexcept;

        /// このフレームで離した。前フレーム true から現フレーム false への変化
        [[nodiscard]] bool IsReleased(GamepadButton b) const noexcept;

        /// -1.0〜1.0 の左スティック、デッドゾーン適用済
        [[nodiscard]] Stick LeftStick() const noexcept;

        /// -1.0〜1.0 の右スティック、デッドゾーン適用済
        [[nodiscard]] Stick RightStick() const noexcept;

        /// 0.0〜1.0 の左トリガー、トリガースレッショルド未満は 0.0
        [[nodiscard]] float LeftTrigger() const noexcept;

        /// 0.0〜1.0 の右トリガー、トリガースレッショルド未満は 0.0
        [[nodiscard]] float RightTrigger() const noexcept;

        /// previous = current 退避後、XInputGetState() をポーリング。未接続時は m_current をリセット
        void Update() noexcept;

    private:
        static constexpr std::size_t kButtonCount = static_cast<std::size_t>(GamepadButton::Count);

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
