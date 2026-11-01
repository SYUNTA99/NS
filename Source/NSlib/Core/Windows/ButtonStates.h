#pragma once

#include "NSlib/Core/Types.h"

#include <array>
#include <cstddef>

namespace NS
{
    namespace OS
    {
        //! 機器のボタンごとに、押している間と押した回数を持つ
        //! TEnum は末尾に Count を持つ enum。First より前の値と範囲の外の値は無視する
        template <typename TEnum, TEnum First>
        class ButtonStates
        {
        public:
            [[nodiscard]] bool IsHeld(TEnum button) const noexcept
            {
                return IsValid(button) && m_held[static_cast<std::size_t>(button)];
            }

            //! 押した瞬間ごとに 1 足した回数。下ろさない。押した瞬間は InputReader で読む
            [[nodiscard]] uint32 PressCount(TEnum button) const noexcept
            {
                if (!IsValid(button))
                {
                    return 0;
                }
                return m_presses[static_cast<std::size_t>(button)];
            }

        protected:
            //! 押していないボタンだけを数える。押したままの繰り返しは数えない
            void Press(TEnum button) noexcept
            {
                if (!IsValid(button))
                {
                    return;
                }
                const std::size_t i = static_cast<std::size_t>(button);
                if (!m_held[i])
                {
                    ++m_presses[i];
                }
                m_held[i] = true;
            }

            void Release(TEnum button) noexcept
            {
                if (IsValid(button))
                {
                    m_held[static_cast<std::size_t>(button)] = false;
                }
            }

            //! 押している間だけを解除する。押した回数は下ろさない
            void ReleaseAll() noexcept { m_held.fill(false); }

            void CopyPressCounts(const ButtonStates& previous) noexcept { m_presses = previous.m_presses; }

        private:
            static constexpr std::size_t k_Count = static_cast<std::size_t>(TEnum::Count);

            // 整数のキャストで作った不正な値による、配列の範囲の外へのアクセスを防ぐ
            [[nodiscard]] static constexpr bool IsValid(TEnum button) noexcept
            {
                const std::size_t i = static_cast<std::size_t>(button);
                return i >= static_cast<std::size_t>(First) && i < k_Count;
            }

            std::array<bool, k_Count> m_held{};
            std::array<uint32, k_Count> m_presses{};
        };
    } // namespace OS
} // namespace NS
