#include "Editor/Undo/RemoveComponentCommand.h"

#include <vector>

namespace NS::Editor
{
    RemoveComponentCommand::RemoveComponentCommand(std::uint32_t targetObjectId, std::size_t componentIndex) noexcept
        : m_targetObjectId(targetObjectId), m_componentIndex(componentIndex)
    {}

    void RemoveComponentCommand::Do(NS::Game::Level::LevelData& level) noexcept
    {
        m_removed.reset();
        const std::size_t index = NS::Game::Level::FindObjectIndexById(level, m_targetObjectId);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        std::vector<NS::Game::Level::ComponentData>& components = level.objects[index].components;
        if (m_componentIndex >= components.size())
            return;
        // 最後の 1 個は消さない。 空構成の object は build で nullptr になり、 不可視で当たりも gizmo 選択も
        // 失う ゴーストとして level に残る
        if (components.size() <= 1)
            return;
        m_removed = components[m_componentIndex]; // erase より先に反射値ごと退避する
        m_removedIndex = m_componentIndex;
        components.erase(components.begin() + static_cast<std::ptrdiff_t>(m_componentIndex));
    }

    void RemoveComponentCommand::Undo(NS::Game::Level::LevelData& level) noexcept
    {
        if (!m_removed)
            return;
        const std::size_t index = NS::Game::Level::FindObjectIndexById(level, m_targetObjectId);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        std::vector<NS::Game::Level::ComponentData>& components = level.objects[index].components;
        const std::size_t at = m_removedIndex < components.size() ? m_removedIndex : components.size();
        components.insert(components.begin() + static_cast<std::ptrdiff_t>(at), *m_removed);
    }

    std::size_t RemoveComponentCommand::EstimatedBytes() const noexcept
    {
        // 反射値の文字列が確保するヒープは概算に含めない
        std::size_t bytes = sizeof(RemoveComponentCommand);
        if (m_removed)
            bytes += NS::Game::Level::EstimatedHeapBytes(*m_removed);
        return bytes;
    }

} // namespace NS::Editor
