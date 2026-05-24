#include "Framework/Scene/Component.h"

#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

#include <cassert>

namespace NS::Scene
{

    Component::Component(GameObject* owner, int priority) noexcept : m_priority(priority)
    {
        if (owner != nullptr)
        {
            owner->RegisterComponent(this);
        }
    }

    Component::~Component() noexcept
    {
        if (m_owner != nullptr)
        {
            m_owner->UnregisterComponent(this);
        }
    }

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
