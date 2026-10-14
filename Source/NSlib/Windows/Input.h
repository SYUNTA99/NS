#pragma once

#include "NSlib/Windows/Gamepad.h"
#include "NSlib/Windows/Keyboard.h"
#include "NSlib/Windows/Mouse.h"

namespace NS::OS
{
    //! @brief UI が取る入力の登録内容
    struct UiCaptureDesc
    {
        bool wantMouse = false;
        bool wantKeyboard = false;
        bool leftButtonToGame = false; // UI がマウスを持つ間も、左ボタンだけゲームへ渡すか
    };

    //! @brief キーボード、マウス、ゲームパッドへのアクセスを統合する入力管理クラス
    class Input
    {
    public:
        //! @brief プロセス全体で共有されるシングルトンインスタンスを取得する
        [[nodiscard]] static Input& Get() noexcept
        {
            static Input s_instance;
            return s_instance;
        }

        [[nodiscard]] NS::OS::Keyboard& Keyboard() noexcept { return m_keyboard; }
        [[nodiscard]] const NS::OS::Keyboard& Keyboard() const noexcept { return m_keyboard; }

        [[nodiscard]] NS::OS::Mouse& Mouse() noexcept { return m_mouse; }
        [[nodiscard]] const NS::OS::Mouse& Mouse() const noexcept { return m_mouse; }

        //! @brief SetGamepadUserIndex で選んだ 1 台のゲームパッドを返す
        [[nodiscard]] NS::OS::Gamepad& Gamepad() noexcept { return m_gamepad; }
        [[nodiscard]] const NS::OS::Gamepad& Gamepad() const noexcept { return m_gamepad; }

        //! XInput の番号 0〜3 に切り替える。状態を初期化し、次の Update から読む
        //! 範囲外は変更せず false
        [[nodiscard]] bool SetGamepadUserIndex(int userIndex) noexcept;

        //! @brief 各入力デバイスの内部状態を更新する
        void Update() noexcept;

        //! 入力を中立へ一時的に差し替える。重ねた回数だけ EndNeutral が必要
        //! 中立中は Update を呼ばない。振動は実機へ送らない
        void BeginNeutral() noexcept;

        //! @brief BeginNeutral で差し替えた機器の状態を元へ戻す
        //! @details 中立でない時は何もしない
        void EndNeutral() noexcept;

        [[nodiscard]] bool IsNeutral() const noexcept { return m_neutralDepth > 0; }

        void SetUiCapture(const UiCaptureDesc& desc) noexcept { m_uiCapture = desc; }

        [[nodiscard]] bool UiWantsMouse() const noexcept { return m_uiCapture.wantMouse; }

        [[nodiscard]] bool UiWantsKeyboard() const noexcept { return m_uiCapture.wantKeyboard; }

        //! @brief ゲームがマウスのボタンを受け取るかを返す
        //! @details UI がマウスを持つ間は受け取らない。ただし左ボタンの受け渡しを登録した間は左だけ受け取る
        //! @return ゲームへ渡す場合 true、それ以外の場合は false
        [[nodiscard]] bool GameReceivesMouseButton(MouseButton button) const noexcept
        {
            if (!m_uiCapture.wantMouse)
            {
                return true;
            }
            return button == MouseButton::Left && m_uiCapture.leftButtonToGame;
        }

    private:
        NS::OS::Keyboard m_keyboard;
        NS::OS::Mouse m_mouse;
        NS::OS::Gamepad m_gamepad;

        UiCaptureDesc m_uiCapture{};

        // 中立の間に控える手元の機器の状態。BeginNeutral で入れ替え、EndNeutral で戻す
        NS::OS::Keyboard m_heldKeyboard;
        NS::OS::Mouse m_heldMouse;
        NS::OS::Gamepad m_heldGamepad;
        int m_neutralDepth = 0; // BeginNeutral を重ねた数
    };

    //! 有効期間中だけ入力を中立にし、破棄時に元へ戻す
    class ScopedNeutralInput
    {
    public:
        ScopedNeutralInput() noexcept { Input::Get().BeginNeutral(); }
        ~ScopedNeutralInput() { Input::Get().EndNeutral(); }

        ScopedNeutralInput(const ScopedNeutralInput&) = delete;
        ScopedNeutralInput& operator=(const ScopedNeutralInput&) = delete;
    };

} // namespace NS::OS
