#pragma once

/// @file PlaceCommand.h
/// @brief 指定 cell に block を配置する Command。 既存 block があれば置換、 Undo で復元

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

        void Do(EditTarget& target) noexcept override;
        void Undo(EditTarget& target) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return sizeof(PlaceCommand); }

    private:
        std::int16_t m_x;
        std::int16_t m_y;
        std::int16_t m_z;
        std::uint16_t m_blockId;
        std::uint8_t m_rotation;
        std::optional<NS::Game::Level::ObjectInstance> m_replaced;
        std::optional<std::uint32_t> m_assignedId; // append 時の識別子。 redo で再利用し参照を壊さない
    };

} // namespace NS::Game::Undo
