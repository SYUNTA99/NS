#include "NSlib/CoreObject/SubObject.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"
#include "NSlib/Windows/Clock.h"

namespace NS
{

    SubObject::SubObject() noexcept = default;

    SubObject::~SubObject() noexcept = default;

    bool SubObject::IsOwnerAppeared() const noexcept
    {
        if (m_owner == nullptr)
        {
            return true;
        }
        return m_owner->IsAppeared();
    }

    Scene* SubObject::OwningScene() const noexcept
    {
        if (m_owner == nullptr)
        {
            return nullptr;
        }
        return m_owner->OwningScene();
    }

    float SubObject::BodyDelta() const noexcept
    {
        if (m_owner == nullptr)
        {
            return OS::FrameTimer::FixedDelta();
        }
        return m_owner->BodyDelta();
    }

    Transform& SubObject::RootTransform() noexcept
    {
        NS_ASSERT(Scene, m_owner != nullptr, "owner の居ない SubObject から RootTransform() を呼んでいる");
        return m_owner->Root();
    }

    const Transform& SubObject::RootTransform() const noexcept
    {
        NS_ASSERT(Scene, m_owner != nullptr, "owner の居ない SubObject から RootTransform() を呼んでいる");
        return m_owner->Root();
    }

} // namespace NS
