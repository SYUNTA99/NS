#pragma once

/// @file PlaceCommand.h
/// @brief 指定 cell に block を配置する Command。 既存 block があれば置換、 Undo で復元

#include "Editor/Undo/ICommand.h"
#include "Game/Level/LevelData.h"

#include <cstdint>
#include <optional>

namespace NS::Editor
{

    class PlaceCommand final : public ICommand
    {
    public:
        /// プロトタイプ配置物を複製し、 cell 座標と回転 step を焼いて置く
        PlaceCommand(NS::Game::Level::ObjectInstance prototype,
                     std::int16_t x,
                     std::int16_t y,
                     std::int16_t z,
                     std::uint8_t rotation) noexcept;

        void Do(NS::Game::Level::LevelData& level) noexcept override;
        void Undo(NS::Game::Level::LevelData& level) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override
        {
            std::size_t bytes = sizeof(PlaceCommand) + NS::Game::Level::EstimatedHeapBytes(m_prototype);
            if (m_replaced)
                bytes += NS::Game::Level::EstimatedHeapBytes(*m_replaced);
            return bytes;
        }

    private:
        std::int16_t m_x;
        std::int16_t m_y;
        std::int16_t m_z;
        std::uint8_t m_rotation;
        NS::Game::Level::ObjectInstance m_prototype; // 配置時に複製する複製元
        std::optional<NS::Game::Level::ObjectInstance> m_replaced;
        std::optional<std::uint32_t> m_assignedObjectId; // append 時の永続 id。 redo で再利用する
    };

} // namespace NS::Editor
