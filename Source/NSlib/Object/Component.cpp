#include "NSlib/Object/Component.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Transform.h"

namespace NS::Obj
{

    Component::Component() noexcept = default;

    Component::~Component() noexcept = default;

    bool Component::IsActive() const noexcept
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

    Scene* Component::OwningScene() const noexcept
    {
        if (m_owner == nullptr)
        {
            return nullptr;
        }
        return m_owner->OwningScene();
    }

    Transform& Component::RootTransform() noexcept
    {
        NS_ASSERT(Scene, m_owner != nullptr, "owner の居ない Component から RootTransform() を呼んでいる");
        return m_owner->Root();
    }

    const Transform& Component::RootTransform() const noexcept
    {
        NS_ASSERT(Scene, m_owner != nullptr, "owner の居ない Component から RootTransform() を呼んでいる");
        return m_owner->Root();
    }

} // namespace NS::Obj
