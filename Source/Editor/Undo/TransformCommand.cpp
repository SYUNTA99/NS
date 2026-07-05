#include "Editor/Undo/TransformCommand.h"

namespace NS::Editor
{

    TransformCommand::TransformCommand(std::uint32_t id,
                                       const NS::GameCore::Level::ObjectInstance& before,
                                       const NS::GameCore::Level::ObjectInstance& after) noexcept
        : m_id(id), m_before(before), m_after(after)
    {}

    void TransformCommand::Do(NS::GameCore::Level::LevelData& level) noexcept
    {
        const std::size_t index = NS::GameCore::Level::FindObjectIndexById(level, m_id);
        if (index == NS::GameCore::Level::kNoObjectIndex)
            return;
        level.objects[index] = m_after;
    }

    void TransformCommand::Undo(NS::GameCore::Level::LevelData& level) noexcept
    {
        const std::size_t index = NS::GameCore::Level::FindObjectIndexById(level, m_id);
        if (index == NS::GameCore::Level::kNoObjectIndex)
            return;
        level.objects[index] = m_before;
    }

} // namespace NS::Editor
