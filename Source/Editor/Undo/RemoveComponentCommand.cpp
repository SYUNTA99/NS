#include "Editor/Undo/RemoveComponentCommand.h"

#include <utility>
#include <vector>

namespace NS::Editor
{
    RemoveComponentCommand::RemoveComponentCommand(std::uint32_t targetObjectId, std::string typeName) noexcept
        : m_targetObjectId(targetObjectId), m_typeName(std::move(typeName))
    {}

    void RemoveComponentCommand::Do(NS::Game::Level::EditTarget& target) noexcept
    {
        m_removed.reset();
        const std::size_t index = NS::Game::Level::IndexOfId(target, m_targetObjectId);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        std::vector<NS::Game::Level::ComponentData>& components = target.level.objects[index].components;
        for (std::size_t i = 0; i < components.size(); ++i)
        {
            if (components[i].typeName == m_typeName)
            {
                m_removed = components[i]; // erase より先に反射値ごと退避する
                m_removedIndex = i;
                components.erase(components.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
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
        std::size_t bytes = sizeof(RemoveComponentCommand) + m_typeName.size();
        if (m_removed)
        {
            bytes += m_removed->typeName.size();
            for (const NS::Game::Level::FieldValue& field : m_removed->fields)
                bytes += sizeof(NS::Game::Level::FieldValue) + field.name.size();
        }
        return bytes;
    }

} // namespace NS::Editor
