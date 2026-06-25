#include "Editor/Undo/PlaceCommand.h"

namespace NS::Editor
{
    PlaceCommand::PlaceCommand(
        std::int16_t x, std::int16_t y, std::int16_t z, std::uint16_t blockId, std::uint8_t rotation) noexcept
        : m_x(x), m_y(y), m_z(z), m_blockId(blockId), m_rotation(static_cast<std::uint8_t>(rotation & 0x03))
    {}

    void PlaceCommand::Do(NS::Game::Level::EditTarget& target) noexcept
    {
        NS::Game::Level::LevelData& level = target.level;
        const std::size_t index = NS::Game::Level::FindGridObjectAtCell(level, m_x, m_y, m_z);
        const NS::Game::Level::ObjectInstance placed =
            NS::Game::Level::MakeGridObject(m_x, m_y, m_z, m_blockId, m_rotation);
        if (index != NS::Game::Level::kNoObjectIndex)
        {
            // 既存 cell の置換は in-place なので id を据え置く
            m_replaced = level.objects[index];
            level.objects[index] = placed;
        }
        else
        {
            m_replaced.reset();
            // redo でも同じ識別子を再利用する (この object を指す TransformCommand を壊さない)
            if (!m_assignedId)
                m_assignedId = target.nextId++;
            level.objects.push_back(placed);
            target.ids.push_back(*m_assignedId);
        }
    }

    void PlaceCommand::Undo(NS::Game::Level::EditTarget& target) noexcept
    {
        NS::Game::Level::LevelData& level = target.level;
        const std::size_t index = NS::Game::Level::FindGridObjectAtCell(level, m_x, m_y, m_z);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        if (m_replaced)
        {
            level.objects[index] = *m_replaced;
        }
        else
        {
            level.objects.erase(level.objects.begin() + static_cast<std::ptrdiff_t>(index));
            target.ids.erase(target.ids.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

} // namespace NS::Editor
