#include "Editor/Undo/RemoveComponentCommand.h"

#include <vector>

namespace NS::Editor
{
    RemoveComponentCommand::RemoveComponentCommand(std::uint32_t targetObjectId, std::size_t componentIndex) noexcept
        : m_targetObjectId(targetObjectId), m_componentIndex(componentIndex)
    {}

    void RemoveComponentCommand::Do(NS::Game::Level::EditTarget& target) noexcept
    {
        m_removed.reset();
        const std::size_t index = NS::Game::Level::IndexOfId(target, m_targetObjectId);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        std::vector<NS::Game::Level::ComponentData>& components = target.level.objects[index].components;
        if (m_componentIndex >= components.size())
            return;
        m_removed = components[m_componentIndex]; // erase より先に反射値ごと退避する
        m_removedIndex = m_componentIndex;
        components.erase(components.begin() + static_cast<std::ptrdiff_t>(m_componentIndex));
    }

    void RemoveComponentCommand::Undo(NS::Game::Level::EditTarget& target) noexcept
    {
        if (!m_removed)
            return;
        const std::size_t index = NS::Game::Level::IndexOfId(target, m_targetObjectId);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        std::vector<NS::Game::Level::ComponentData>& components = target.level.objects[index].components;
        const std::size_t at = m_removedIndex < components.size() ? m_removedIndex : components.size();
        components.insert(components.begin() + static_cast<std::ptrdiff_t>(at), *m_removed);
    }

    std::size_t RemoveComponentCommand::EstimatedBytes() const noexcept
    {
        // 反射値の文字列が確保するヒープは概算に含めない
        std::size_t bytes = sizeof(RemoveComponentCommand);
        if (m_removed)
        {
            bytes += m_removed->typeName.size();
            for (const NS::Game::Level::FieldValue& field : m_removed->fields)
                bytes += sizeof(NS::Game::Level::FieldValue) + field.name.size();
        }
        return bytes;
    }

} // namespace NS::Editor
