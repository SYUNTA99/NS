#pragma once

#include <array>
#include <cstddef>

namespace NS::Platform
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

        //! @brief 最新のハードウェア状態を同期する
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
    };

} // namespace NS::Platform