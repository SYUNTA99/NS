#include "Game/Undo/TransformCommand.h"

#include "Game/Undo/EditTarget.h"

namespace NS::Game::Undo
{

    TransformCommand::TransformCommand(std::uint32_t id,
                                       const NS::Game::Level::ObjectInstance& before,
                                       const NS::Game::Level::ObjectInstance& after) noexcept
        : m_id(id), m_before(before), m_after(after)
    {}

    void TransformCommand::Do(EditTarget& target) noexcept
    {
        const std::size_t index = IndexOfId(target, m_id);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        target.level.objects[index] = m_after;
    }

    void TransformCommand::Undo(EditTarget& target) noexcept
    {
        const std::size_t index = IndexOfId(target, m_id);
        if (index == NS::Game::Level::kNoObjectIndex)
            return;
        target.level.objects[index] = m_before;
    }

} // namespace NS::Game::Undo
