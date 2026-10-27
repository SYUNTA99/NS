#pragma once

#include "NSlib/Core/Types.h"
#include "NSlib/Core/Windows/Gamepad.h"
#include "NSlib/Core/Windows/Keyboard.h"
#include "NSlib/Core/Windows/Mouse.h"

#include <array>
#include <cstddef>

namespace NS
{
    namespace OS
    {
        /// 手元の機器のキーとボタンごとの押した回数
        struct PressCounts
        {
            std::array<uint32, static_cast<std::size_t>(Key::Count)> keys{};
            std::array<uint32, static_cast<std::size_t>(MouseButton::Count)> mouseButtons{};
            std::array<uint32, static_cast<std::size_t>(GamepadButton::Count)> gamepadButtons{};
            std::array<uint32, static_cast<std::size_t>(GamepadTrigger::Count)> gamepadTriggers{};
        };

        /// InputReader::Read が返す、前の Read から後に押されたキーとボタン
        class InputPresses
        {
        public:
            [[nodiscard]] bool WasPressed(Key k) const noexcept;
            [[nodiscard]] bool WasPressed(MouseButton b) const noexcept;
            [[nodiscard]] bool WasPressed(GamepadButton b) const noexcept;
            /// しきい値を超えて引いたか
            [[nodiscard]] bool WasPressed(GamepadTrigger t) const noexcept;

        private:
            friend class InputReader;

            std::array<bool, static_cast<std::size_t>(Key::Count)> m_keys{};
            std::array<bool, static_cast<std::size_t>(MouseButton::Count)> m_mouseButtons{};
            std::array<bool, static_cast<std::size_t>(GamepadButton::Count)> m_gamepadButtons{};
            std::array<bool, static_cast<std::size_t>(GamepadTrigger::Count)> m_gamepadTriggers{};
        };

        /// 機器の押した回数を、前に Read した時の回数と比べて、押した瞬間を返す
        /// 押した瞬間を読む物は、それぞれ 1 つ持ち、自分の時計で Read を呼ぶ。
        /// 機器は回数を下ろさないので、ほかの InputReader が読んでも押しは消えず、
        /// Read の間に何回固定フレームが進んでも押しを落とさない。
        /// 機器は Input::Get() の手元の機器を読む
        class InputReader
        {
        public:
            /// 作る前の押しは返さない
            InputReader() noexcept;

            /// 前の Read (無ければ作った時) から後に押された物を返す
            /// 中立 (Input::BeginNeutral) の間は何も返さず、前に読んだ回数も進めない。
            /// 中立の間に手元の機器へ届いた押しは、明けた後の Read が返す
            [[nodiscard]] InputPresses Read() noexcept;

            /// 前の Read から後の押しを、返さずに読んだ事にする
            void Discard() noexcept;

        private:
            PressCounts m_seen{}; // 前に Read した時の押した回数
        };
    } // namespace OS
} // namespace NS
