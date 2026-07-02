#include "Editor/Undo/AddObjectCommand.h"

#include "Game/Level/LevelData.h"

namespace NS::Editor
{
    AddObjectCommand::AddObjectCommand(const NS::Game::Level::ObjectInstance& object) noexcept : m_object(object) {}

    void AddObjectCommand::Do(NS::Game::Level::EditTarget& target) noexcept
    {
        // この object を指す TransformCommand を壊さないよう redo でも同じ識別子を再利用する
        if (!m_assignedId)
            m_assignedId = target.nextId++;
        // 永続 id も初回だけ採番して m_object に焼き、redo で別の id にならないようにする
        if (m_object.objectId == 0)
            m_object.objectId = NS::Game::Level::AllocateObjectId(target.level);
        target.level.objects.push_back(m_object);
        target.ids.push_back(*m_assignedId);
    }

    void AddObjectCommand::Undo(NS::Game::Level::EditTarget& target) noexcept
    {
        if (!m_assignedId)
            return;
        const std::size_t index = NS::Game::Level::IndexOfId(target, *m_assignedId);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        target.level.objects.erase(target.level.objects.begin() + static_cast<std::ptrdiff_t>(index));
        target.ids.erase(target.ids.begin() + static_cast<std::ptrdiff_t>(index));
    }

} // namespace NS::Editor
