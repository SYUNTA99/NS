#include "Editor/Undo/AddObjectCommand.h"

#include "Game/Level/LevelData.h"

namespace NS::Editor
{
    AddObjectCommand::AddObjectCommand(const NS::Game::Level::ObjectInstance& object) noexcept : m_object(object) {}

    void AddObjectCommand::Do(NS::Game::Level::LevelData& level) noexcept
    {
        // 永続 id は初回だけ採番して m_object に焼き、redo で別の id にならないようにする
        if (m_object.objectId == NS::Game::Level::kNoObjectId)
            m_object.objectId = NS::Game::Level::AllocateObjectId(level);
        level.objects.push_back(m_object);
    }

    void AddObjectCommand::Undo(NS::Game::Level::LevelData& level) noexcept
    {
        const std::size_t index = NS::Game::Level::FindObjectIndexById(level, m_object.objectId);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        level.objects.erase(level.objects.begin() + static_cast<std::ptrdiff_t>(index));
    }

} // namespace NS::Editor
