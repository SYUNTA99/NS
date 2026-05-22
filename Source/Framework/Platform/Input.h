#pragma once

#include <array>

#include <Framework/Platform/Gamepad.h>
#include <Framework/Platform/Keyboard.h>
#include <Framework/Platform/Mouse.h>

namespace NS::Platform
{

    /// 入力デバイスの集約。Application が所有し、毎フレーム頭で Update() を呼ぶ。
    /// Keyboard / Mouse / Gamepad のサブクラスへの参照を提供する。
    /// 共通基底クラスは持たない ( /  整合)。
    class Input
    {
    public:
        Input();

        [[nodiscard]] NS::Platform::Keyboard& Keyboard() noexcept { return m_keyboard; }
        [[nodiscard]] const NS::Platform::Keyboard& Keyboard() const noexcept { return m_keyboard; }

        [[nodiscard]] NS::Platform::Mouse& Mouse() noexcept { return m_mouse; }
        [[nodiscard]] const NS::Platform::Mouse& Mouse() const noexcept { return m_mouse; }

        /// 現状は 1 スロットのみ。将来 4 スロット対応は配列拡張で API 互換。
        /// index が範囲外の場合は index 0 を返す (no-throw、未接続として振る舞う)。
        [[nodiscard]] NS::Platform::Gamepad& Gamepad(int index = 0) noexcept;
        [[nodiscard]] const NS::Platform::Gamepad& Gamepad(int index = 0) const noexcept;

        /// 全サブクラスの Update を呼ぶ (フレーム頭で 1 回)。
        void Update() noexcept;

    private:
        static constexpr std::size_t kGamepadSlotCount = 1;

        NS::Platform::Keyboard m_keyboard;
        NS::Platform::Mouse m_mouse;
        std::array<NS::Platform::Gamepad, kGamepadSlotCount> m_gamepads;
    };

} // namespace NS::Platform
