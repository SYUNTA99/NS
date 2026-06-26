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

        /// kind から既定プロトタイプを起こして置く薄経路。 結果は prototype 版と同一
        PlaceCommand(
            std::int16_t x, std::int16_t y, std::int16_t z, std::uint16_t blockId, std::uint8_t rotation) noexcept;

        void Do(NS::Game::Level::EditTarget& target) noexcept override;
        void Undo(NS::Game::Level::EditTarget& target) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return sizeof(PlaceCommand); }

    private:
        std::int16_t m_x;
        std::int16_t m_y;
        std::int16_t m_z;
        std::uint8_t m_rotation;
        NS::Game::Level::ObjectInstance m_prototype; // 配置時に複製する複製元
        std::optional<NS::Game::Level::ObjectInstance> m_replaced;
        std::optional<std::uint32_t> m_assignedId; // append 時の識別子。 redo で再利用し参照を壊さない
    };

} // namespace NS::Editor
