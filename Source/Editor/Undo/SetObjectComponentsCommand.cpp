#include "Editor/Undo/SetObjectComponentsCommand.h"

namespace NS::Editor
{
    SetObjectComponentsCommand::SetObjectComponentsCommand(
        std::uint32_t targetObjectId, std::vector<NS::GameCore::Level::ComponentData> newComponents) noexcept
        : m_targetObjectId(targetObjectId), m_newComponents(std::move(newComponents))
    {}

    void SetObjectComponentsCommand::Do(NS::GameCore::Level::LevelData& level) noexcept
    {
        const std::size_t index = NS::GameCore::Level::FindObjectIndexById(level, m_targetObjectId);
        if (index == NS::GameCore::Level::kNoObjectIndex)
            return;
        std::vector<NS::GameCore::Level::ComponentData>& components = level.objects[index].components;
        m_oldComponents = components;
        components = m_newComponents;
    }

    void SetObjectComponentsCommand::Undo(NS::GameCore::Level::LevelData& level) noexcept
    {
        const std::size_t index = NS::GameCore::Level::FindObjectIndexById(level, m_targetObjectId);
        if (index == NS::GameCore::Level::kNoObjectIndex)
            return;
        level.objects[index].components = m_oldComponents;
    }

    std::size_t SetObjectComponentsCommand::EstimatedBytes() const noexcept
    {
        // 反射値の文字列が確保するヒープは概算に含めない
        std::size_t bytes = sizeof(SetObjectComponentsCommand);
        for (const NS::GameCore::Level::ComponentData& comp : m_newComponents)
            bytes += NS::GameCore::Level::EstimatedHeapBytes(comp);
        for (const NS::GameCore::Level::ComponentData& comp : m_oldComponents)
            bytes += NS::GameCore::Level::EstimatedHeapBytes(comp);
        return bytes;
    }

} // namespace NS::Editor
