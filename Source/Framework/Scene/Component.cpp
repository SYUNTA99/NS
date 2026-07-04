#include "Framework/Scene/Component.h"

#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Transform.h"

#include <cassert>

namespace NS::Scene
{

    Component::Component(int priority) noexcept : m_priority(priority) {}

    Component::~Component() noexcept = default;

    bool Component::IsA(const ReflectionInfo* target) const noexcept
    {
        if (target == nullptr)
            return false;
        // 反射情報は型ごとに 1 実体なのでアドレス比較で足りる。鎖は現状深さ 2 が最大
        for (const ReflectionInfo* info = GetReflection(); info != nullptr; info = info->base)
        {
            if (info == target)
                return true;
        }
        return false;
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
