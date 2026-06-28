#include "Editor/Undo/DuplicateObjectCommand.h"

#include "Game/Level/LevelData.h"

#include <utility>

namespace NS::Editor
{
    DuplicateObjectCommand::DuplicateObjectCommand(std::uint32_t sourceObjectId) noexcept
        : m_sourceObjectId(sourceObjectId)
    {}

    void DuplicateObjectCommand::Do(NS::Game::Level::EditTarget& target) noexcept
    {
        const std::size_t srcIndex = NS::Game::Level::IndexOfId(target, m_sourceObjectId);
        if (srcIndex == NS::Game::Level::kNoObjectIndex)
            return;
        // push_back の再確保で source 参照が無効化される前にコピーを確定させる
        NS::Game::Level::ObjectInstance copy = target.level.objects[srcIndex];
        if (!m_assignedId)
            m_assignedId = target.nextId++;
        target.level.objects.push_back(std::move(copy));
        target.ids.push_back(*m_assignedId);
    }

    void DuplicateObjectCommand::Undo(NS::Game::Level::EditTarget& target) noexcept
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
