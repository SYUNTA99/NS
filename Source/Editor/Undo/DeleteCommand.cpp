#include "Editor/Undo/DeleteCommand.h"

namespace NS::Editor
{
    DeleteCommand::DeleteCommand(std::int16_t x, std::int16_t y, std::int16_t z) noexcept : m_x(x), m_y(y), m_z(z) {}

    void DeleteCommand::Do(NS::GameCore::Level::LevelData& level) noexcept
    {
        const std::size_t index = NS::GameCore::Level::FindObjectAtCell(level, m_x, m_y, m_z);
        if (index == NS::GameCore::Level::kNoObjectIndex)
        {
            m_deleted.reset();
            return;
        }
        m_deleted = level.objects[index];
        level.objects.erase(level.objects.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void DeleteCommand::Undo(NS::GameCore::Level::LevelData& level) noexcept
    {
        if (!m_deleted)
            return;
        // 復元する ObjectInstance が永続 id ごと焼き込んでいるため、 この object を指す参照は壊れない
        level.objects.push_back(*m_deleted);
    }

} // namespace NS::Editor
