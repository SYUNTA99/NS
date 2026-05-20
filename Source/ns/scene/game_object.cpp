#include "ns/scene/game_object.h"

#include "ns/scene/component.h"

#include <algorithm>

namespace ns::scene
{

    void GameObject::SetParent(GameObject* parent) noexcept
    {
        if (parent == m_parent)
            return;

        DetachFromParent();
        m_parent = parent;
        if (m_parent != nullptr)
            m_parent->m_children.push_back(this);

        // Transform 階層も同期。null で root 化。
        m_root.SetParent(parent != nullptr ? &parent->m_root : nullptr);
    }

    void GameObject::DetachFromParent() noexcept
    {
        if (m_parent == nullptr)
            return;
        auto& siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        m_parent = nullptr;
    }

    void GameObject::RegisterComponent(Component* comp) noexcept
    {
        if (comp == nullptr)
            return;
        comp->AttachOwner(this);
        m_components.push_back(comp);
    }

    void GameObject::OnStart()
    {
        for (Component* comp : m_components)
        {
            if (comp != nullptr && comp->IsActive())
                comp->OnStart();
        }
    }

    void GameObject::OnUpdate(float dt)
    {
        for (Component* comp : m_components)
        {
            if (comp != nullptr && comp->IsActive())
                comp->OnUpdate(dt);
        }
    }

    void GameObject::OnEndPlay()
    {
        // 逆順で OnEndPlay (後から register したもの先に廃棄)
        for (auto it = m_components.rbegin(); it != m_components.rend(); ++it)
        {
            Component* comp = *it;
            if (comp != nullptr)
                comp->OnEndPlay();
        }
    }

} // namespace ns::scene
