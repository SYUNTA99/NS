#include "NSlib/Object/SubObject.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Transform.h"
#include "NSlib/Windows/Clock.h"

namespace NS::Obj
{

    SubObject::SubObject() noexcept = default;

    SubObject::~SubObject() noexcept = default;

    bool SubObject::IsActive() const noexcept
    {
        if (!m_enabled || !m_active)
        {
            return false;
        }
        // owner に着く前は IsActiveSelf の値だけで答える。組み立て途中の問い合わせをここで落とさない
        if (m_owner == nullptr)
        {
            return true;
        }
        return m_owner->IsActiveInHierarchy();
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
            return NS::OS::FrameTimer::FixedDelta();
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

} // namespace NS::Obj
