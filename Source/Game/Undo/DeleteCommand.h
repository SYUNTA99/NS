#pragma once

/// @file DeleteCommand.h
/// @brief 指定 cell の block を削除する Command。 削除前 entry を保存し Undo で push_back 復元

#include "Game/Level/LevelData.h"
#include "Game/Undo/ICommand.h"

#include <cstdint>
#include <optional>

namespace NS::Game::Undo
{

    class DeleteCommand final : public ICommand
    {
    public:
        DeleteCommand(std::int16_t x, std::int16_t y, std::int16_t z) noexcept;

        void Do(NS::Game::Level::LevelData& level) noexcept override;
        void Undo(NS::Game::Level::LevelData& level) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return sizeof(DeleteCommand); }

    private:
        std::int16_t m_x;
        std::int16_t m_y;
        std::int16_t m_z;
        std::optional<NS::Game::Level::BlockEntry> m_deleted;
    };

} // namespace NS::Game::Undo
