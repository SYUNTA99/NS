#include "Game/Undo/AddObjectCommand.h"

namespace NS::Game::Undo
{
    AddObjectCommand::AddObjectCommand(const NS::Game::Level::ObjectInstance& object) noexcept : m_object(object) {}

    void AddObjectCommand::Do(EditTarget& target) noexcept
    {
        // redo でも同じ識別子を再利用する (この object を指す TransformCommand を壊さない)
        if (!m_assignedId)
            m_assignedId = target.nextId++;
        target.level.objects.push_back(m_object);
        target.ids.push_back(*m_assignedId);
    }

    void AddObjectCommand::Undo(EditTarget& target) noexcept
    {
        if (!m_assignedId)
            return;
        const std::size_t index = IndexOfId(target, *m_assignedId);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        target.level.objects.erase(target.level.objects.begin() + static_cast<std::ptrdiff_t>(index));
        target.ids.erase(target.ids.begin() + static_cast<std::ptrdiff_t>(index));
    }

} // namespace NS::Game::Undo
