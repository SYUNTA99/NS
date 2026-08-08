#include "Runtime/Object/Component.h"

#include "Runtime/Core/Assert.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Transform.h"

namespace NS::Object
{

    Component::Component(int priority) noexcept : m_priority(priority) {}

    Component::~Component() noexcept = default;

    bool Component::IsActive() const noexcept
    {
        if (!m_enabled || !m_active)
            return false;
        // owner に着く前は IsActiveSelf の値だけで答える。 組み立て途中の問い合わせをここで落とさない
        if (m_owner == nullptr)
            return true;
        return m_owner->IsActiveInHierarchy();
    }

    bool Component::IsA(const ReflectionInfo* target) const noexcept
    {
        if (target == nullptr)
            return false;
        // リフレクション情報は型ごとに 1 つだけなのでアドレス比較で足りる。鎖は現状深さ 2 が最大
        for (const ReflectionInfo* info = GetReflection(); info != nullptr; info = info->base)
        {
            if (info == target)
                return true;
        }
        return false;
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

} // namespace NS::Object
