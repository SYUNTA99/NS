#pragma once

/// @file Input.h
/// @brief NS::Platform::Input — Keyboard / Mouse / Gamepad をまとめる窓口
///
/// @details Application が所有し、 fixed step ループの頭で `Update()` を 1 回呼ぶ
/// 共通基底は持たず、 サブクラスへの参照取得 API のみ提供する
/// 状態更新は WndProc / XInput ポーリング経由で各サブクラスに直接行う

#include <array>

#include <Framework/Platform/Gamepad.h>
#include <Framework/Platform/Keyboard.h>
#include <Framework/Platform/Mouse.h>

namespace NS::Platform
{

    /// 入力デバイスの集約。Keyboard / Mouse / Gamepad への参照を提供する静的クラス
    class Input
    {
    public:
        Input() noexcept;

        [[nodiscard]] NS::Platform::Keyboard& Keyboard() noexcept { return m_keyboard; }
        [[nodiscard]] const NS::Platform::Keyboard& Keyboard() const noexcept { return m_keyboard; }

        [[nodiscard]] NS::Platform::Mouse& Mouse() noexcept { return m_mouse; }
        [[nodiscard]] const NS::Platform::Mouse& Mouse() const noexcept { return m_mouse; }

        /// index 範囲外は 0 を返す。将来の複数スロット対応は配列拡張で API 互換
        [[nodiscard]] NS::Platform::Gamepad& Gamepad(int index = 0) noexcept;
        [[nodiscard]] const NS::Platform::Gamepad& Gamepad(int index = 0) const noexcept;

        /// 全サブクラスの Update を呼ぶ (フレーム頭で 1 回)
        void Update() noexcept;

        /// 開発用 UI (editor の ImGui) が入力を掴んでいるかを毎フレーム反映する窓口
        /// editor が描画後に push し、Window / gameplay 側の入力ゲートはここを読む
        /// 出荷 build には editor が無いため常に false のまま (全入力がゲームに届く)
        void SetUiCapture(bool wantMouse, bool wantKeyboard) noexcept
        {
            m_uiWantsMouse = wantMouse;
            m_uiWantsKeyboard = wantKeyboard;
        }

        /// UI がマウスを掴んでいるか。直前フレームの状態 (NewFrame 後に push されるため 1 フレーム遅延)
        [[nodiscard]] bool UiWantsMouse() const noexcept { return m_uiWantsMouse; }
        /// UI がキーボードを掴んでいるか。テキスト入力中のゲーム操作抑止に使う
        [[nodiscard]] bool UiWantsKeyboard() const noexcept { return m_uiWantsKeyboard; }

    private:
        static constexpr std::size_t kGamepadSlotCount = 1;

        NS::Platform::Keyboard m_keyboard;
        NS::Platform::Mouse m_mouse;
        std::array<NS::Platform::Gamepad, kGamepadSlotCount> m_gamepads;

        bool m_uiWantsMouse = false;
        bool m_uiWantsKeyboard = false;
    };

} // namespace NS::Platform
