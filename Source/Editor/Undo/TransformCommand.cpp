#include "Editor/Undo/TransformCommand.h"

namespace NS::Editor
{

    TransformCommand::TransformCommand(std::uint32_t id,
                                       const NS::Scene::ObjectData& before,
                                       const NS::Scene::ObjectData& after) noexcept
        : m_id(id), m_before(before), m_after(after)
    {}

    void TransformCommand::Do(NS::Scene::SceneData& level) noexcept
    {
        const std::size_t index = NS::Scene::FindObjectIndexById(level, m_id);
        if (index == NS::Scene::kNoObjectIndex)
            return;
        level.objects[index] = m_after;
    }

    void TransformCommand::Undo(NS::Scene::SceneData& level) noexcept
    {
        const std::size_t index = NS::Scene::FindObjectIndexById(level, m_id);
        if (index == NS::Scene::kNoObjectIndex)
            return;
        level.objects[index] = m_before;
    }

} // namespace NS::Editor
