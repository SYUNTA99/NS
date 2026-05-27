#pragma once

/// @file PlaceCommand.h
/// @brief 指定 cell に block を配置する Command。 既存 block があれば置換、 Undo で復元。

#include "Game/Level/LevelData.h"
#include "Game/Undo/ICommand.h"

#include <cstdint>
#include <optional>

namespace NS::Game::Undo
{

    class PlaceCommand final : public ICommand
    {
    public:
        PlaceCommand(
            std::int16_t x, std::int16_t y, std::int16_t z, std::uint16_t blockId, std::uint8_t rotation) noexcept;

        void Do(NS::Game::Level::LevelData& level) noexcept override;
        void Undo(NS::Game::Level::LevelData& level) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return sizeof(PlaceCommand); }

    private:
        std::int16_t m_x;
        std::int16_t m_y;
        std::int16_t m_z;
        std::uint16_t m_blockId;
        std::uint8_t m_rotation;
        std::optional<NS::Game::Level::BlockEntry> m_replaced;
    };

} // namespace NS::Game::Undo
