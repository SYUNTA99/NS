#include "Runtime/Object/GameObject.h"

#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/TransformComponent.h"

#include <algorithm>

namespace NS::Object
{

    GameObject::GameObject() noexcept
    {
        m_transform = &AddComponentUnchecked<TransformComponent>()->Root();
    }

    GameObject::~GameObject() noexcept
    {
        DetachFromParent();
        for (GameObject* child : m_children)
        {
            if (child == nullptr)
                continue;
            child->m_parent = nullptr;
            child->m_transform->SetParent(nullptr);
        }
    }

    bool GameObject::IsActiveInHierarchy() const noexcept
    {
        // 1 つでも active が false なら効かない。 循環は SetParent が作らせないので必ず root で止まる
        for (const GameObject* node = this; node != nullptr; node = node->m_parent)
        {
            if (!node->m_activeSelf)
                return false;
        }
        return true;
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

        Transform* parentRoot = nullptr;
        if (parent != nullptr)
            parentRoot = parent->m_transform;
        m_transform->SetParent(parentRoot);
    }

    void GameObject::DetachFromParent() noexcept
    {
        if (m_parent == nullptr)
            return;
        auto& siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        m_parent = nullptr;
    }

    void GameObject::AttachOwnedComponent(Component* comp) noexcept
    {
        comp->AttachOwner(this);
        m_components.push_back(comp);
        std::stable_sort(m_components.begin(), m_components.end(), [](const Component* a, const Component* b) noexcept {
            return a->Priority() < b->Priority();
        });
    }

    void GameObject::OnStart()
    {
        // 後で SetActive(true) されても初期化済になるよう、IsActive に依らず全件呼出
        for (Component* comp : m_components)
        {
            if (comp != nullptr)
                comp->OnStart();
        }
    }

    void GameObject::OnUpdate()
    {
        for (Component* comp : m_components)
        {
            if (comp != nullptr && comp->IsActive())
                comp->OnUpdate();
        }
    }

    void GameObject::OnEndPlay()
    {
        // 後から登録したものを先に廃棄する
        for (auto it = m_components.rbegin(); it != m_components.rend(); ++it)
        {
            Component* comp = *it;
            if (comp != nullptr)
                comp->OnEndPlay();
        }
    }

} // namespace NS::Object
