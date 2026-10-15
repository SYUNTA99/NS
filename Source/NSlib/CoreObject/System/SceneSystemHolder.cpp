#include "NSlib/CoreObject/System/SceneSystemHolder.h"

#include "NSlib/CoreObject/Scene/Scene.h"

namespace NS
{
    void SceneSystemHolder::Clear() noexcept
    {
        while (!m_objects.empty())
        {
            m_scene.RemoveSystemTick(*m_objects.back().second);
            m_objects.pop_back();
        }
    }
} // namespace NS
