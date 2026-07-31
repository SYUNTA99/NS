#pragma once

#include "Runtime/Platform/Gamepad.h"
#include "Runtime/Platform/Keyboard.h"
#include "Runtime/Platform/Mouse.h"

#include <array>

namespace NS::Platform
{
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

        //! @brief 各入力デバイスの内部状態を更新する
        void Update() noexcept;

        //! @brief UIがマウスやキーボードの入力をキャプチャしている状態を登録する
        //! @note 出荷ビルド等でUIが存在しない環境では常にfalseとなり、すべての入力がゲーム側へ渡る前提となる
        void SetUiCapture(bool wantMouse, bool wantKeyboard) noexcept
        {
            m_uiWantsMouse = wantMouse;
            m_uiWantsKeyboard = wantKeyboard;
        }

        //! UIがマウス入力をキャプチャしているかどうかを返す
        [[nodiscard]] bool UiWantsMouse() const noexcept { return m_uiWantsMouse; }

        //! UIがキーボード入力をキャプチャしているかどうかを返す
        [[nodiscard]] bool UiWantsKeyboard() const noexcept { return m_uiWantsKeyboard; }

    private:
        static constexpr std::size_t k_GamepadSlotCount = 1;

        NS::Platform::Keyboard m_keyboard;
        NS::Platform::Mouse m_mouse;
        std::array<NS::Platform::Gamepad, k_GamepadSlotCount> m_gamepads;

        bool m_uiWantsMouse = false;
        bool m_uiWantsKeyboard = false;
    };

} // namespace NS::Platform