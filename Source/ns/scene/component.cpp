#include "ns/scene/component.h"

#include "ns/scene/game_object.h"
#include "ns/scene/transform.h"

#include <cassert>

namespace ns::scene
{

    Transform& Component::RootTransform() noexcept
    {
        assert(m_owner != nullptr && "Component is not registered to any GameObject");
        return m_owner->Root();
    }

    const Transform& Component::RootTransform() const noexcept
    {
        assert(m_owner != nullptr && "Component is not registered to any GameObject");
        return m_owner->Root();
    }

} // namespace ns::scene
