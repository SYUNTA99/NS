#pragma once

/// @file DeleteCommand.h
/// @brief 指定 cell の block を削除する Command。 削除前 entry を保存し Undo で push_back 復元

#include "Editor/Undo/ICommand.h"
#include "GameCore/Level/LevelData.h"

#include <cstdint>
#include <optional>

namespace NS::Editor
{

    class DeleteCommand final : public ICommand
    {
    public:
        DeleteCommand(std::int16_t x, std::int16_t y, std::int16_t z) noexcept;

        void Do(NS::GameCore::Level::LevelData& level) noexcept override;
        void Undo(NS::GameCore::Level::LevelData& level) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return sizeof(DeleteCommand); }

    private:
        std::int16_t m_x;
        std::int16_t m_y;
        std::int16_t m_z;
        std::optional<NS::GameCore::Level::ObjectInstance> m_deleted;
    };

} // namespace NS::Editor
