#include "Editor/Undo/DuplicateObjectCommand.h"

#include "Framework/Scene/SceneData.h"

namespace NS::Editor
{
    DuplicateObjectCommand::DuplicateObjectCommand(std::uint32_t sourceObjectId) noexcept
        : m_sourceObjectId(sourceObjectId)
    {}

    void DuplicateObjectCommand::Do(NS::Scene::SceneData& level) noexcept
    {
        const std::size_t srcIndex = NS::Scene::FindObjectIndexById(level, m_sourceObjectId);
        if (srcIndex == NS::Scene::kNoObjectIndex)
            return;
        // push_back の再確保で source 参照が無効化される前にコピーを確定させる
        NS::Scene::ObjectData copy = level.objects[srcIndex];
        // 複製元の永続 id を引き継ぐと一意性が壊れるので、初回に新 id を採番し redo で再利用する
        if (!m_assignedObjectId)
            m_assignedObjectId = NS::Scene::AllocateObjectId(level);
        copy.objectId = *m_assignedObjectId;
        level.objects.push_back(std::move(copy));
    }

    void DuplicateObjectCommand::Undo(NS::Scene::SceneData& level) noexcept
    {
        if (!m_assignedObjectId)
            return;
        const std::size_t index = NS::Scene::FindObjectIndexById(level, *m_assignedObjectId);
        if (index == NS::Scene::kNoObjectIndex)
            return;
        level.objects.erase(level.objects.begin() + static_cast<std::ptrdiff_t>(index));
    }

} // namespace NS::Editor
