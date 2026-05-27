#include "Game/Undo/DeleteCommand.h"

#include <algorithm>

namespace NS::Game::Undo
{
    namespace
    {
        auto FindCell(NS::Game::Level::LevelData& level, std::int16_t x, std::int16_t y, std::int16_t z)
        {
            return std::find_if(level.blocks.begin(), level.blocks.end(), [x, y, z](const auto& b) {
                return b.x == x && b.y == y && b.z == z;
            });
        }
    } // namespace

    DeleteCommand::DeleteCommand(std::int16_t x, std::int16_t y, std::int16_t z) noexcept : m_x(x), m_y(y), m_z(z) {}

    void DeleteCommand::Do(NS::Game::Level::LevelData& level) noexcept
    {
        auto it = FindCell(level, m_x, m_y, m_z);
        if (it == level.blocks.end())
        {
            m_deleted.reset();
            return;
        }
        m_deleted = *it;
        level.blocks.erase(it);
    }

    void DeleteCommand::Undo(NS::Game::Level::LevelData& level) noexcept
    {
        if (!m_deleted)
            return;
        level.blocks.push_back(*m_deleted);
    }

} // namespace NS::Game::Undo
