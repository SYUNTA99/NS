#include "Editor/Undo/AddObjectCommand.h"

#include "Framework/Scene/SceneData.h"

namespace NS::Editor
{
    AddObjectCommand::AddObjectCommand(const NS::Scene::ObjectData& object) noexcept : m_object(object) {}

    void AddObjectCommand::Do(NS::Scene::SceneData& level) noexcept
    {
        // 永続 id は初回だけ採番して m_object に焼き、redo で別の id にならないようにする
        if (m_object.objectId == NS::Scene::kNoObjectId)
            m_object.objectId = NS::Scene::AllocateObjectId(level);
        level.objects.push_back(m_object);
    }

    void AddObjectCommand::Undo(NS::Scene::SceneData& level) noexcept
    {
        const std::size_t index = NS::Scene::FindObjectIndexById(level, m_object.objectId);
        if (index == NS::Scene::kNoObjectIndex)
            return;
        level.objects.erase(level.objects.begin() + static_cast<std::ptrdiff_t>(index));
    }

} // namespace NS::Editor
