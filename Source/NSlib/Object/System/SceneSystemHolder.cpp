#include "NSlib/Object/System/SceneSystemHolder.h"

#include "NSlib/Object/Scene/Scene.h"

namespace NS::Obj
{
    void SceneSystemHolder::Clear() noexcept
    {
        while (!m_objects.empty())
        {
            m_scene.RemoveSystemTick(*m_objects.back().second);
            m_objects.pop_back();
        }
    }
} // namespace NS::Obj
