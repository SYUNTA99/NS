#pragma once

#include "NSlib/Core/Types.h"
#include <array>
#include <cstddef>

namespace NS::OS
{
    //! @brief ゲームパッドのボタン識別子
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

    //! @brief 正規化されたアナログスティックの入力値
    struct Stick
    {
        float x = 0.0f; // -1.0〜1.0
        float y = 0.0f; // -1.0〜1.0
    };

    //! @brief パッドの 2 つのモーターの速さ
    struct GamepadVibration
    {
        float left = 0.0f;  //!< 低い周波数のモーターの速さ。0.0〜1.0
        float right = 0.0f; //!< 高い周波数のモーターの速さ。0.0〜1.0
    };

    //! @brief ゲームパッドの状態を管理するクラス
    //! @note 内部でポーリングするので、メインループのフレーム頭で Update() を呼ぶ
    class Gamepad
    {
    public:
        Gamepad() = default;
        explicit Gamepad(int userIndex) noexcept;

        [[nodiscard]] bool IsConnected() const noexcept;

        //! @brief ボタンが現在のフレームで新たに押されたかどうかを判定する
        [[nodiscard]] bool IsPressed(GamepadButton b) const noexcept;

        [[nodiscard]] bool IsHeld(GamepadButton b) const noexcept;

        //! @brief ボタンが現在のフレームで離されたかどうかを判定する
        [[nodiscard]] bool IsReleased(GamepadButton b) const noexcept;

        [[nodiscard]] Stick LeftStick() const noexcept;

        [[nodiscard]] Stick RightStick() const noexcept;

        //! 0〜1 の入力。XInput のしきい値未満は 0
        [[nodiscard]] float LeftTrigger() const noexcept;

        //! 正規化の範囲としきい値は LeftTrigger と同じ
        [[nodiscard]] float RightTrigger() const noexcept;

        //! @brief Update で状態を読む XInput のユーザー番号を返す
        [[nodiscard]] int UserIndex() const noexcept;

        //! left は低周波、right は高周波の強さ 0〜1。範囲外・非有限値は変更せず false
        //! 次の Update で実機へ送り、継続中は更新ごとに設定する
        [[nodiscard]] bool SetVibration(float left, float right) noexcept;

        //! @brief 書かれている振動の速さを返す
        //! @return 最後に書いた速さ。書かれずに Update を過ぎた後と、止めた後は 0
        [[nodiscard]] GamepadVibration Vibration() const noexcept;

        //! 更新を待たずに実機の振動を止める
        void StopVibration() noexcept;

        //! 入力を更新し、前回の更新後に指定されなかった振動を止める
        void Update() noexcept;

    private:
        static constexpr std::size_t k_ButtonCount = static_cast<std::size_t>(GamepadButton::Count);

        int m_userIndex = 0;
        bool m_connected = false;
        std::array<bool, k_ButtonCount> m_current{};
        std::array<bool, k_ButtonCount> m_previous{};
        Stick m_leftStick{};
        Stick m_rightStick{};
        float m_leftTrigger = 0.0f;
        float m_rightTrigger = 0.0f;
        GamepadVibration m_vibration{};
        bool m_vibrationWritten = false; // 前の Update の後に振動が書かれたか
        // 実機へ最後に送った速さ。XInput の 0〜65535
        NS::uint16 m_sentLeftMotor = 0;
        NS::uint16 m_sentRightMotor = 0;
    };

} // namespace NS::OS
