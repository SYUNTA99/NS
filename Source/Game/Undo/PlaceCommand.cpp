#include "Game/Undo/PlaceCommand.h"

#include <algorithm>

namespace NS::Game::Undo
{
    namespace
    {
        /// 同 cell (x, y, z) の BlockEntry を線形検索する
        auto FindCell(NS::Game::Level::LevelData& level, std::int16_t x, std::int16_t y, std::int16_t z)
        {
            return std::find_if(level.blocks.begin(), level.blocks.end(), [x, y, z](const auto& b) {
                return b.x == x && b.y == y && b.z == z;
            });
        }
    } // namespace

    PlaceCommand::PlaceCommand(
        std::int16_t x, std::int16_t y, std::int16_t z, std::uint16_t blockId, std::uint8_t rotation) noexcept
        : m_x(x), m_y(y), m_z(z), m_blockId(blockId), m_rotation(static_cast<std::uint8_t>(rotation & 0x03))
    {}

    void PlaceCommand::Do(NS::Game::Level::LevelData& level) noexcept
    {
        auto it = FindCell(level, m_x, m_y, m_z);
        if (it != level.blocks.end())
        {
            m_replaced = *it;
            it->blockId = m_blockId;
            it->rotation = m_rotation;
        }
        else
        {
            m_replaced.reset();
            level.blocks.push_back({m_x, m_y, m_z, m_blockId, m_rotation, 0});
        }
    }

    void PlaceCommand::Undo(NS::Game::Level::LevelData& level) noexcept
    {
        auto it = FindCell(level, m_x, m_y, m_z);
        if (it == level.blocks.end())
            return;
        if (m_replaced)
        {
            *it = *m_replaced;
        }
        else
        {
            level.blocks.erase(it);
        }
    }

} // namespace NS::Game::Undo
