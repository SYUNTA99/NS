#include "Runtime/Object/GameObject.h"

#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectName.h"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace NS::Obj
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
            {
				continue;
            }
            child->m_parent = nullptr;
            child->m_transform->SetParent(nullptr);
        }
    }

    bool GameObject::IsActiveInHierarchy() const noexcept
    {
        // 1 つでも active が false なら効かない。循環は SetParent が作らせないので必ず root で止まる
        for (const GameObject* node = this; node != nullptr; node = node->m_parent)
        {
            if (!node->m_activeSelf)
            {
				return false;
            }
        }
        return true;
    }

    void GameObject::SetParent(GameObject* parent) noexcept
    {
        if (parent == this || parent == m_parent)
        {
            return;
        }


        // 循環防止: parent の祖先に this がいたら拒否
        for (GameObject* p = parent; p != nullptr; p = p->m_parent)
        {
            if (p == this)
            {
                return;
            }
        }

        DetachFromParent();
        m_parent = parent;
        if (m_parent != nullptr)
        {
            m_parent->m_children.push_back(this);
        }

        Transform* parentRoot = nullptr;
        if (parent != nullptr)
        {
            parentRoot = parent->m_transform;
        }
        m_transform->SetParent(parentRoot);
    }

    void GameObject::DetachFromParent() noexcept
    {
        if (m_parent == nullptr)
        {
			return;
        }
        std::vector<GameObject*>& siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        m_parent = nullptr;
    }

    Component* GameObject::FindComponentByName(std::string_view name) const noexcept
    {
        for (Component* comp : m_components)
        {
            if (comp != nullptr && comp->Name() == name)
            {
                return comp;
            }
        }
        return nullptr;
    }

    Component* GameObject::FindComponentById(std::uint32_t id) const noexcept
    {
        if (id == 0)
        {
            return nullptr;
        }
        for (Component* comp : m_components)
        {
            if (comp != nullptr && comp->Id() == id)
            {
                return comp;
            }
        }
        return nullptr;
    }

    void GameObject::RenameComponent(Component& comp, std::string_view name)
    {
        if (comp.Owner() != this)
        {
            return;
        }
        // 自分の今の名前は数えない。同じ名前へ付け直した時に _1 が付いてしまう
        std::unordered_set<std::string> used;
        used.reserve(m_components.size());
        for (const Component* other : m_components)
        {
            if (other != nullptr && other != &comp)
            {
                used.insert(other->Name());
            }
        }
        std::string_view base = name;
        if (base.empty())
        {
            base = comp.ClassName();
        }
        comp.SetName(MakeUniqueObjectName(base, used));
    }

    void GameObject::AttachOwnedComponent(Component* comp)
    {
        comp->AttachOwner(this);
        // 名前は作った時点で付ける。データから組む時は、組み立て側が後から保存された名前へ付け直す
        RenameComponent(*comp, {});
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
            {
                comp->OnStart();
            }
        }
    }

    void GameObject::OnUpdate()
    {
        for (Component* comp : m_components)
        {
            if (comp != nullptr && comp->IsActive())
            {
                comp->OnUpdate();
            }
        }
    }

    void GameObject::OnEndPlay()
    {
        // 更新の並びと逆順に呼ぶ
        for (std::vector<Component*>::reverse_iterator it = m_components.rbegin(); it != m_components.rend(); ++it)
        {
            Component* comp = *it;
            if (comp != nullptr)
            {
                comp->OnEndPlay();
            }
        }
    }

} // namespace NS::Obj
