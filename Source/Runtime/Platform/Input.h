#pragma once

#include "Runtime/Platform/Gamepad.h"
#include "Runtime/Platform/Keyboard.h"
#include "Runtime/Platform/Mouse.h"

#include <array>

namespace NS::Platform
{
    //! @brief UI が取る入力の登録内容
    struct UiCaptureDesc
    {
        bool wantMouse = false;        // UI がマウスを持つか
        bool wantKeyboard = false;     // UI がキーボードを持つか
        bool leftButtonToGame = false; // UI がマウスを持つ間も、左ボタンだけゲームへ渡すか
    };

    //! @brief キーボード、マウス、ゲームパッドへのアクセスを統合する入力管理クラス
    class Input
    {
    public:
        Input() noexcept;

        //! @brief プロセス全体で共有されるシングルトンインスタンスを取得する
        [[nodiscard]] static Input& Get() noexcept
        {
            static Input s_instance;
            return s_instance;
        }

        [[nodiscard]] NS::Platform::Keyboard& Keyboard() noexcept { return m_keyboard; }
        [[nodiscard]] const NS::Platform::Keyboard& Keyboard() const noexcept { return m_keyboard; }

        [[nodiscard]] NS::Platform::Mouse& Mouse() noexcept { return m_mouse; }
        [[nodiscard]] const NS::Platform::Mouse& Mouse() const noexcept { return m_mouse; }

        //! @brief 指定されたインデックスのゲームパッドを取得する
        [[nodiscard]] NS::Platform::Gamepad& Gamepad(int index = 0) noexcept;
        [[nodiscard]] const NS::Platform::Gamepad& Gamepad(int index = 0) const noexcept;

        //! @brief Gamepad(0) が読む XInput のユーザー番号を切り替える
        //! @details 0〜3 の外は何もしない。切り替えると状態を空に戻し、次の Update から読み直す
        //! @param[in] userIndex XInput のユーザー番号 (0〜3)
        //! @return 切り替えた場合 true、それ以外の場合は false
        [[nodiscard]] bool SetGamepadUserIndex(int userIndex) noexcept;

        //! @brief 各入力デバイスの内部状態を更新する
        void Update() noexcept;

        //! @brief UIがマウスやキーボードの入力をキャプチャしている状態を登録する
        //! @note 出荷ビルド等でUIが存在しない環境では常にfalseとなり、すべての入力がゲーム側へ渡る前提となる
        //! @param[in] desc UI が取る入力。既定値はすべてゲームへ渡す
        void SetUiCapture(const UiCaptureDesc& desc) noexcept
        {
            m_uiWantsMouse = desc.wantMouse;
            m_uiWantsKeyboard = desc.wantKeyboard;
            m_leftButtonToGame = desc.leftButtonToGame;
        }

        //! UIがマウス入力をキャプチャしているかどうかを返す
        [[nodiscard]] bool UiWantsMouse() const noexcept { return m_uiWantsMouse; }

        //! UIがキーボード入力をキャプチャしているかどうかを返す
        [[nodiscard]] bool UiWantsKeyboard() const noexcept { return m_uiWantsKeyboard; }

        //! @brief ゲームがマウスのボタンを受け取るかを返す
        //! @details UI がマウスを持つ間は受け取らない。ただし左ボタンの受け渡しを登録した間は左だけ受け取る
        //! @param[in] button 調べるボタン
        //! @return ゲームへ渡す場合 true、それ以外の場合は false
        [[nodiscard]] bool GameReceivesMouseButton(MouseButton button) const noexcept
        {
            if (!m_uiWantsMouse)
            {
                return true;
            }
            return button == MouseButton::Left && m_leftButtonToGame;
        }

    private:
        static constexpr std::size_t k_GamepadSlotCount = 1;

        NS::Platform::Keyboard m_keyboard;
        NS::Platform::Mouse m_mouse;
        std::array<NS::Platform::Gamepad, k_GamepadSlotCount> m_gamepads;

        bool m_uiWantsMouse = false;
        bool m_uiWantsKeyboard = false;
        bool m_leftButtonToGame = false; // UI がマウスを持つ間も左ボタンだけゲームへ渡すか
    };

} // namespace NS::Platform