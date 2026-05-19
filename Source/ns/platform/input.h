#pragma once

#include <array>

#include <ns/platform/gamepad.h>
#include <ns/platform/keyboard.h>
#include <ns/platform/mouse.h>

namespace ns::platform
{

    /// 入力デバイスの集約。Application が所有し、毎フレーム頭で Update() を呼ぶ。
    /// Keyboard / Mouse / Gamepad のサブクラスへの参照を提供する。
    /// 共通基底クラスは持たない ( /  整合)。
    class Input
    {
    public:
        Input();

        [[nodiscard]] ns::platform::Keyboard& Keyboard() noexcept { return m_keyboard; }
        [[nodiscard]] const ns::platform::Keyboard& Keyboard() const noexcept { return m_keyboard; }

        [[nodiscard]] ns::platform::Mouse& Mouse() noexcept { return m_mouse; }
        [[nodiscard]] const ns::platform::Mouse& Mouse() const noexcept { return m_mouse; }

        /// 現状は 1 スロットのみ。将来 4 スロット対応は配列拡張で API 互換。
        /// index が範囲外の場合は index 0 を返す (no-throw、未接続として振る舞う)。
        [[nodiscard]] ns::platform::Gamepad& Gamepad(int index = 0) noexcept;
        [[nodiscard]] const ns::platform::Gamepad& Gamepad(int index = 0) const noexcept;

        /// 全サブクラスの Update を呼ぶ (フレーム頭で 1 回)。
        void Update() noexcept;

    private:
        static constexpr std::size_t kGamepadSlotCount = 1;

        ns::platform::Keyboard m_keyboard;
        ns::platform::Mouse m_mouse;
        std::array<ns::platform::Gamepad, kGamepadSlotCount> m_gamepads;
    };

} // namespace ns::platform
