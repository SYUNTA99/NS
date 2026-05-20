#include "ns/scene/component.h"

#include "ns/scene/game_object.h"
#include "ns/scene/transform.h"

namespace ns::scene
{

    Transform& Component::RootTransform() noexcept
    {
        return m_owner->Root();
    }

    const Transform& Component::RootTransform() const noexcept
    {
        return m_owner->Root();
    }

} // namespace ns::scene
