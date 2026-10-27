#include "NSlib/Core/Windows/InputReader.h"

#include "NSlib/Core/Windows/Input.h"

namespace NS::OS
{
        namespace
        {
            // 前に読んだ回数より今の回数が多い物を押したとする。読んだ回数は今の回数へ進める
            template <std::size_t N>
            void ComparePresses(const std::array<uint32, N>& now,
                                std::array<uint32, N>& seen,
                                std::array<bool, N>& outPressed) noexcept
            {
                for (std::size_t i = 0; i < N; ++i)
                {
                    outPressed[i] = now[i] > seen[i];
                    seen[i] = now[i];
                }
            }

            template <class TEnum, std::size_t N>
            [[nodiscard]] bool PressedAt(const std::array<bool, N>& pressed, TEnum value) noexcept
            {
                const std::size_t i = static_cast<std::size_t>(value);
                if (i >= N)
                {
                    return false;
                }
                return pressed[i];
            }
        } // namespace

        bool InputPresses::WasPressed(Key k) const noexcept { return PressedAt(m_keys, k); }

        bool InputPresses::WasPressed(MouseButton b) const noexcept { return PressedAt(m_mouseButtons, b); }

        bool InputPresses::WasPressed(GamepadButton b) const noexcept { return PressedAt(m_gamepadButtons, b); }

        bool InputPresses::WasPressed(GamepadTrigger t) const noexcept { return PressedAt(m_gamepadTriggers, t); }

        InputReader::InputReader() noexcept : m_seen(Input::Get().CurrentPressCounts()) {}

        InputPresses InputReader::Read() noexcept
        {
            const Input& input = Input::Get();
            InputPresses presses;
            if (input.IsNeutral())
            {
                return presses;
            }
            const PressCounts now = input.CurrentPressCounts();
            ComparePresses(now.keys, m_seen.keys, presses.m_keys);
            ComparePresses(now.mouseButtons, m_seen.mouseButtons, presses.m_mouseButtons);
            ComparePresses(now.gamepadButtons, m_seen.gamepadButtons, presses.m_gamepadButtons);
            ComparePresses(now.gamepadTriggers, m_seen.gamepadTriggers, presses.m_gamepadTriggers);
            return presses;
        }

        void InputReader::Discard() noexcept { m_seen = Input::Get().CurrentPressCounts(); }
} // namespace NS::OS
