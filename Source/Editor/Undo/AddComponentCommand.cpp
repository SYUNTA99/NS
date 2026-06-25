#include "Editor/Undo/AddComponentCommand.h"

#include "Game/Level/LevelData.h"

#include <utility>
#include <vector>

namespace NS::Editor
{
    AddComponentCommand::AddComponentCommand(std::uint32_t targetObjectId, std::string typeName) noexcept
        : m_targetObjectId(targetObjectId), m_typeName(std::move(typeName))
    {}

    void AddComponentCommand::Do(NS::Game::Level::EditTarget& target) noexcept
    {
        m_addedIndex.reset();
        const std::size_t index = NS::Game::Level::IndexOfId(target, m_targetObjectId);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        std::vector<NS::Game::Level::ComponentData>& components = target.level.objects[index].components;
        // 同型がすでにあっても重ねて足す。 単一強制が要る型は上位の出し分けで扱う
        components.push_back(NS::Game::Level::ComponentData{m_typeName});
        m_addedIndex = components.size() - 1;
    }

    void AddComponentCommand::Undo(NS::Game::Level::EditTarget& target) noexcept
    {
        if (!m_addedIndex)
            return;
        const std::size_t index = NS::Game::Level::IndexOfId(target, m_targetObjectId);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        std::vector<NS::Game::Level::ComponentData>& components = target.level.objects[index].components;
        if (*m_addedIndex < components.size())
            components.erase(components.begin() + static_cast<std::ptrdiff_t>(*m_addedIndex));
    }

    std::size_t AddComponentCommand::EstimatedBytes() const noexcept
    {
        return sizeof(AddComponentCommand) + m_typeName.size();
    }

} // namespace NS::Editor
