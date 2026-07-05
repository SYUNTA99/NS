#include "Editor/Undo/AddObjectCommand.h"

#include "GameCore/Level/LevelData.h"

namespace NS::Editor
{
    AddObjectCommand::AddObjectCommand(const NS::GameCore::Level::ObjectInstance& object) noexcept : m_object(object) {}

    void AddObjectCommand::Do(NS::GameCore::Level::LevelData& level) noexcept
    {
        // 永続 id は初回だけ採番して m_object に焼き、redo で別の id にならないようにする
        if (m_object.objectId == NS::GameCore::Level::kNoObjectId)
            m_object.objectId = NS::GameCore::Level::AllocateObjectId(level);
        level.objects.push_back(m_object);
    }

    void AddObjectCommand::Undo(NS::GameCore::Level::LevelData& level) noexcept
    {
        const std::size_t index = NS::GameCore::Level::FindObjectIndexById(level, m_object.objectId);
        if (index == NS::GameCore::Level::kNoObjectIndex)
            return;
        level.objects.erase(level.objects.begin() + static_cast<std::ptrdiff_t>(index));
    }

} // namespace NS::Editor
