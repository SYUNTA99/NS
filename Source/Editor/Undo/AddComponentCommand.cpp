#include "Editor/Undo/AddComponentCommand.h"

#include "GameCore/Level/LevelData.h"

namespace NS::Editor
{
    AddComponentCommand::AddComponentCommand(std::uint32_t targetObjectId,
                                             NS::GameCore::Level::ComponentData payload) noexcept
        : m_targetObjectId(targetObjectId), m_payload(std::move(payload))
    {}

    void AddComponentCommand::Do(NS::GameCore::Level::LevelData& level) noexcept
    {
        m_addedIndex.reset();
        const std::size_t index = NS::GameCore::Level::FindObjectIndexById(level, m_targetObjectId);
        if (index == NS::GameCore::Level::kNoObjectIndex)
            return;
        std::vector<NS::GameCore::Level::ComponentData>& components = level.objects[index].components;
        // 同型がすでにあっても重ねて足す。 単一強制が要る型は上位の出し分けで扱う
        components.push_back(m_payload);
        m_addedIndex = components.size() - 1;
    }

    void AddComponentCommand::Undo(NS::GameCore::Level::LevelData& level) noexcept
    {
        if (!m_addedIndex)
            return;
        const std::size_t index = NS::GameCore::Level::FindObjectIndexById(level, m_targetObjectId);
        if (index == NS::GameCore::Level::kNoObjectIndex)
            return;
        std::vector<NS::GameCore::Level::ComponentData>& components = level.objects[index].components;
        if (*m_addedIndex < components.size())
            components.erase(components.begin() + static_cast<std::ptrdiff_t>(*m_addedIndex));
    }

    std::size_t AddComponentCommand::EstimatedBytes() const noexcept
    {
        // 反射値の文字列が確保するヒープは概算に含めない
        return sizeof(AddComponentCommand) + NS::GameCore::Level::EstimatedHeapBytes(m_payload);
    }

} // namespace NS::Editor
