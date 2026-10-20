#pragma once

#include "NSlib/Windows/Gamepad.h"
#include "NSlib/Windows/Keyboard.h"
#include "NSlib/Windows/Mouse.h"

#include <windows.h>

namespace NS
{
    namespace OS
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

            // 読む機器を返す。中立の間は中立の機器を返す
            [[nodiscard]] OS::Keyboard& Keyboard() noexcept
            {
                if (IsNeutral())
                {
                    return m_neutralKeyboard;
                }
                return m_keyboard;
            }
            [[nodiscard]] const OS::Keyboard& Keyboard() const noexcept
            {
                if (IsNeutral())
                {
                    return m_neutralKeyboard;
                }
                return m_keyboard;
            }

            [[nodiscard]] OS::Mouse& Mouse() noexcept
            {
                if (IsNeutral())
                {
                    return m_neutralMouse;
                }
                return m_mouse;
            }
            [[nodiscard]] const OS::Mouse& Mouse() const noexcept
            {
                if (IsNeutral())
                {
                    return m_neutralMouse;
                }
                return m_mouse;
            }

            //! @brief SetGamepadUserIndex で選んだ 1 台のゲームパッドを返す
            [[nodiscard]] OS::Gamepad& Gamepad() noexcept
            {
                if (IsNeutral())
                {
                    return m_neutralGamepad;
                }
                return m_gamepad;
            }
            [[nodiscard]] const OS::Gamepad& Gamepad() const noexcept
            {
                if (IsNeutral())
                {
                    return m_neutralGamepad;
                }
                return m_gamepad;
            }

            //! XInput の番号 0〜3 に切り替える。状態を初期化し、次の Update から読む
            //! 範囲外は変更せず false
            [[nodiscard]] bool SetGamepadUserIndex(int userIndex) noexcept;

            //! @brief 各入力デバイスの内部状態を更新する
            void Update() noexcept;

            //! 手元のマウスの相対移動を切り替える。中立の間も手元のマウスへ書く
            void SetMouseRelativeMode(bool enabled) noexcept { m_mouse.SetRelativeMode(enabled); }

            //! 読む側へ何も押していない機器を見せる。重ねた回数だけ EndNeutral が必要
            //! 手元の機器は入れ替えないので、中立の間の OS の入力と相対移動は手元の機器へ書かれる
            //! 中立中は Update を呼ばない。振動は実機へ送らない
            void BeginNeutral() noexcept;

            //! @brief 読む側へ手元の機器を見せる状態へ戻す
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
            // OS の入力は中立の間も手元の機器へ書くので、読む側を通さずに手元の機器へ触れる
            friend void DispatchWin32MessageToInput(Input& input, UINT msg, WPARAM wparam, LPARAM lparam) noexcept;

            // 手元の機器。OS の入力・Update・相対移動の書き先で、中立の間も替わらない
            OS::Keyboard m_keyboard;
            OS::Mouse m_mouse;
            OS::Gamepad m_gamepad;

            UiCaptureDesc m_uiCapture{};

            // 中立の間に読む側へ見せる機器。BeginNeutral で作り直す
            OS::Keyboard m_neutralKeyboard;
            OS::Mouse m_neutralMouse;
            OS::Gamepad m_neutralGamepad;
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
    } // namespace OS
} // namespace NS
