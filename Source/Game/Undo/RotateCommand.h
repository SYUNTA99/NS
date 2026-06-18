#pragma once

/// @file RotateCommand.h
/// @brief 指定 cell の rotation を 90° 単位で増減する Command。 4 回 Do で 1 周

#include "Game/Undo/ICommand.h"

#include <cstdint>
#include <optional>

namespace NS::Game::Undo
{

    class RotateCommand final : public ICommand
    {
    public:
        /// `delta` は ±1 (90° 刻み)。 結果の rotation は mod 4
        RotateCommand(std::int16_t x, std::int16_t y, std::int16_t z, std::int8_t delta) noexcept;

        void Do(EditTarget& target) noexcept override;
        void Undo(EditTarget& target) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return sizeof(RotateCommand); }

    private:
        std::int16_t m_x;
        std::int16_t m_y;
        std::int16_t m_z;
        std::int8_t m_delta;
        std::optional<std::uint8_t> m_prevRotation;
    };

} // namespace NS::Game::Undo
