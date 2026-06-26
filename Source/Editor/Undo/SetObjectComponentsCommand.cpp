#include "Editor/Undo/SetObjectComponentsCommand.h"

#include <utility>
#include <vector>

namespace NS::Editor
{
    SetObjectComponentsCommand::SetObjectComponentsCommand(
        std::uint32_t targetObjectId, std::vector<NS::Game::Level::ComponentData> newComponents) noexcept
        : m_targetObjectId(targetObjectId), m_newComponents(std::move(newComponents))
    {}

    void SetObjectComponentsCommand::Do(NS::Game::Level::EditTarget& target) noexcept
    {
        const std::size_t index = NS::Game::Level::IndexOfId(target, m_targetObjectId);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        std::vector<NS::Game::Level::ComponentData>& components = target.level.objects[index].components;
        m_oldComponents = components; // 置換前を退避してから差し替える
        components = m_newComponents;
    }

    void SetObjectComponentsCommand::Undo(NS::Game::Level::EditTarget& target) noexcept
    {
        const std::size_t index = NS::Game::Level::IndexOfId(target, m_targetObjectId);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        target.level.objects[index].components = m_oldComponents;
    }

    std::size_t SetObjectComponentsCommand::EstimatedBytes() const noexcept
    {
        // 反射値の文字列が確保するヒープは概算に含めない
        std::size_t bytes = sizeof(SetObjectComponentsCommand);
        const auto accumulate = [](const std::vector<NS::Game::Level::ComponentData>& list) {
            std::size_t total = 0;
            for (const NS::Game::Level::ComponentData& comp : list)
            {
                total += comp.typeName.size();
                for (const NS::Game::Level::FieldValue& field : comp.fields)
                    total += sizeof(NS::Game::Level::FieldValue) + field.name.size();
            }
            return total;
        };
        bytes += accumulate(m_newComponents) + accumulate(m_oldComponents);
        return bytes;
    }

} // namespace NS::Editor
