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

    private:
        static constexpr std::size_t kGamepadSlotCount = 1;

        NS::Platform::Keyboard m_keyboard;
        NS::Platform::Mouse m_mouse;
        std::array<NS::Platform::Gamepad, kGamepadSlotCount> m_gamepads;
    };

} // namespace NS::Platform
