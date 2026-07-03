#include "Editor/Undo/TransformCommand.h"

namespace NS::Editor
{

    TransformCommand::TransformCommand(std::uint32_t id,
                                       const NS::Game::Level::ObjectInstance& before,
                                       const NS::Game::Level::ObjectInstance& after) noexcept
        : m_id(id), m_before(before), m_after(after)
    {}

    void TransformCommand::Do(NS::Game::Level::LevelData& level) noexcept
    {
        const std::size_t index = NS::Game::Level::FindObjectIndexById(level, m_id);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        level.objects[index] = m_after;
    }

    void TransformCommand::Undo(NS::Game::Level::LevelData& level) noexcept
    {
        const std::size_t index = NS::Game::Level::FindObjectIndexById(level, m_id);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        level.objects[index] = m_before;
    }

} // namespace NS::Editor
