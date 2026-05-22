#include "Framework/Scene/Component.h"

#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

#include <cassert>

namespace NS::Scene
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

} // namespace NS::Scene
