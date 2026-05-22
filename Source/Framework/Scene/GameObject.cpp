#include "Framework/Scene/GameObject.h"

#include "Framework/Scene/Component.h"

#include <algorithm>

namespace NS::Scene
{

    GameObject::~GameObject() noexcept
    {
        DetachFromParent();
        for (GameObject* child : m_children)
        {
            if (child == nullptr)
                continue;
            child->m_parent = nullptr;
            child->m_root.SetParent(nullptr);
        }
    }

    void GameObject::SetParent(GameObject* parent) noexcept
    {
        if (parent == this || parent == m_parent)
            return;

        // 循環防止: parent の祖先に this がいたら拒否
        for (GameObject* p = parent; p != nullptr; p = p->m_parent)
        {
            if (p == this)
                return;
        }

        DetachFromParent();
        m_parent = parent;
        if (m_parent != nullptr)
            m_parent->m_children.push_back(this);

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

    void GameObject::UnregisterComponent(Component* comp) noexcept
    {
        if (comp == nullptr)
            return;
        auto it = std::remove(m_components.begin(), m_components.end(), comp);
        if (it == m_components.end())
            return;
        m_components.erase(it, m_components.end());
        comp->AttachOwner(nullptr);
    }

    void GameObject::OnStart()
    {
        // 後で SetActive(true) されても初期化済になるよう、IsActive に依らず全件呼出。
        for (Component* comp : m_components)
        {
            if (comp != nullptr)
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

} // namespace NS::Scene
