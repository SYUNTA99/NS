#pragma once

#include <ns/platform/keyboard.h>

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

        /// 全サブクラスの Update を呼ぶ (フレーム頭で 1 回)。
        void Update() noexcept;

    private:
        ns::platform::Keyboard m_keyboard;
    };

} // namespace ns::platform
