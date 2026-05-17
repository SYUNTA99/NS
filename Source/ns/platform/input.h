#pragma once

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
        Input() = default;

        [[nodiscard]] ns::platform::Keyboard& Keyboard() noexcept { return m_keyboard; }
        [[nodiscard]] const ns::platform::Keyboard& Keyboard() const noexcept { return m_keyboard; }

        [[nodiscard]] ns::platform::Mouse& Mouse() noexcept { return m_mouse; }
        [[nodiscard]] const ns::platform::Mouse& Mouse() const noexcept { return m_mouse; }

        /// 全サブクラスの Update を呼ぶ (フレーム頭で 1 回)。
        void Update() noexcept;

    private:
        ns::platform::Keyboard m_keyboard;
        ns::platform::Mouse m_mouse;
    };

} // namespace ns::platform
