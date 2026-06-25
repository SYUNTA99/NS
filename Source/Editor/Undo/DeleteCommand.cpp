#include "Editor/Undo/DeleteCommand.h"

namespace NS::Editor
{
    DeleteCommand::DeleteCommand(std::int16_t x, std::int16_t y, std::int16_t z) noexcept : m_x(x), m_y(y), m_z(z) {}

    void DeleteCommand::Do(NS::Game::Level::EditTarget& target) noexcept
    {
        NS::Game::Level::LevelData& level = target.level;
        const std::size_t index = NS::Game::Level::FindGridObjectAtCell(level, m_x, m_y, m_z);
        if (index == NS::Game::Level::kNoObjectIndex)
        {
            m_deleted.reset();
            return;
        }
        m_deleted = level.objects[index];
        m_deletedId = target.ids[index];
        level.objects.erase(level.objects.begin() + static_cast<std::ptrdiff_t>(index));
        target.ids.erase(target.ids.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void DeleteCommand::Undo(NS::Game::Level::EditTarget& target) noexcept
    {
        if (!m_deleted)
            return;
        // 削除前の識別子を保ったまま末尾へ復元する (この object を指す TransformCommand を壊さない)
        target.level.objects.push_back(*m_deleted);
        if (m_deletedId)
            target.ids.push_back(*m_deletedId);
        else
            target.ids.push_back(target.nextId++);
    }

} // namespace NS::Editor
