#pragma once

/// @file RotateCommand.h
/// @brief 指定 cell の rotation を 90° 単位で増減する Command。 4 回 Do で 1 周

#include "Editor/Undo/ICommand.h"

#include <cstdint>
#include <optional>

namespace NS::Editor
{

    class RotateCommand final : public ICommand
    {
    public:
        /// `delta` は 90° 刻みの ±1。 結果の rotation は mod 4
        RotateCommand(std::int16_t x, std::int16_t y, std::int16_t z, std::int8_t delta) noexcept;

        void Do(NS::Game::Level::LevelData& level) noexcept override;
        void Undo(NS::Game::Level::LevelData& level) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return sizeof(RotateCommand); }

    private:
        std::int16_t m_x;
        std::int16_t m_y;
        std::int16_t m_z;
        std::int8_t m_delta;
        std::optional<std::uint8_t> m_prevRotation;
    };

} // namespace NS::Editor
