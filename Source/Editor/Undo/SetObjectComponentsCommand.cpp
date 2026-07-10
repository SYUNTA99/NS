#include "Editor/Undo/SetObjectComponentsCommand.h"

namespace NS::Editor
{
    SetObjectComponentsCommand::SetObjectComponentsCommand(
        std::uint32_t targetObjectId, std::vector<NS::Scene::ComponentData> newComponents) noexcept
        : m_targetObjectId(targetObjectId), m_newComponents(std::move(newComponents))
    {}

    void SetObjectComponentsCommand::Do(NS::Scene::SceneData& level) noexcept
    {
        const std::size_t index = NS::Scene::FindObjectIndexById(level, m_targetObjectId);
        if (index == NS::Scene::kNoObjectIndex)
            return;
        std::vector<NS::Scene::ComponentData>& components = level.objects[index].components;
        m_oldComponents = components;
        components = m_newComponents;
    }

    void SetObjectComponentsCommand::Undo(NS::Scene::SceneData& level) noexcept
    {
        const std::size_t index = NS::Scene::FindObjectIndexById(level, m_targetObjectId);
        if (index == NS::Scene::kNoObjectIndex)
            return;
        level.objects[index].components = m_oldComponents;
    }

    std::size_t SetObjectComponentsCommand::EstimatedBytes() const noexcept
    {
        // 反射値の文字列が確保するヒープは概算に含めない
        std::size_t bytes = sizeof(SetObjectComponentsCommand);
        for (const NS::Scene::ComponentData& comp : m_newComponents)
            bytes += NS::Scene::EstimatedHeapBytes(comp);
        for (const NS::Scene::ComponentData& comp : m_oldComponents)
            bytes += NS::Scene::EstimatedHeapBytes(comp);
        return bytes;
    }

} // namespace NS::Editor
