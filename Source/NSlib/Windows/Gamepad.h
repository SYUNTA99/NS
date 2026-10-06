#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

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

        //! @brief デバイスの接続状態を返す
        [[nodiscard]] bool IsConnected() const noexcept;

        //! @brief ボタンが現在のフレームで新たに押されたかどうかを判定する
        [[nodiscard]] bool IsPressed(GamepadButton b) const noexcept;

        //! @brief ボタンが現在押され続けているかどうかを判定する
        [[nodiscard]] bool IsHeld(GamepadButton b) const noexcept;

        //! @brief ボタンが現在のフレームで離されたかどうかを判定する
        [[nodiscard]] bool IsReleased(GamepadButton b) const noexcept;

        //! @brief 左スティックの入力を取得する
        [[nodiscard]] Stick LeftStick() const noexcept;

        //! @brief 右スティックの入力を取得する
        [[nodiscard]] Stick RightStick() const noexcept;

        //! @brief 左トリガーの入力を取得する
        [[nodiscard]] float LeftTrigger() const noexcept;

        //! @brief 右トリガーの入力を取得する
        [[nodiscard]] float RightTrigger() const noexcept;

        //! @brief Update で状態を読む XInput のユーザー番号を返す
        [[nodiscard]] int UserIndex() const noexcept;

        //! @brief 振動の速さを書く
        //! @details 範囲の外か非数は何も変えない。実機へは次の Update で送る。振動を続ける間は Update ごとに書き直す
        //! @param[in] left 低い周波数のモーターの速さ (0.0〜1.0)
        //! @param[in] right 高い周波数のモーターの速さ (0.0〜1.0)
        //! @return 書いた場合 true、それ以外の場合は false
        [[nodiscard]] bool SetVibration(float left, float right) noexcept;

        //! @brief 書かれている振動の速さを返す
        //! @return 最後に書いた速さ。書かれずに Update を過ぎた後と、止めた後は 0
        [[nodiscard]] GamepadVibration Vibration() const noexcept;

        //! @brief 振動を止める
        //! @details 速さを 0 にし、実機へ 0 でない速さを送っていた場合はその場で 0 を送る。Update が来ない時に使う
        void StopVibration() noexcept;

        //! @brief 最新のハードウェア状態を同期する
        //! @details 前の Update の後に書かれなかった振動は 0 にする。
        //! 繋がっていて、送った速さと違う場合だけ実機へ送る
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
        std::uint16_t m_sentLeftMotor = 0;
        std::uint16_t m_sentRightMotor = 0;
    };

} // namespace NS::OS