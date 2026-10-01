#include "Runtime/Object/Actor.h"

#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/Animation.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/CapsuleCollider.h"
#include "Runtime/Object/Components/HitReaction.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/Model.h"
#include "Runtime/Object/Components/Shadow.h"
#include "Runtime/Object/Components/StateMachineComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Scene/Scene.h"

#include <algorithm>

namespace NS::Obj
{

    Actor::Actor() noexcept
    {
        m_rootPart = std::make_unique<TransformComponent>();
        m_transform = &m_rootPart->Root();
        AttachFixedComponent(*m_rootPart);
        Appear();
    }

    void Actor::ForEachPart(const PartVisitor& visitor) const
    {
        visitor("Transform", *m_rootPart);
        const auto visit = [&visitor](std::string_view name, Component* part) {
            if (part != nullptr)
            {
                visitor(name, *part);
            }
        };
        visit("Model", m_model.get());
        visit("Animation", m_animation.get());
        visit("Shadow", m_shadow.get());
        visit("Collider", m_collider.get());
        visit("Collision", m_collision.get());
        visit("BodySensor", m_bodySensor.get());
        visit("AttackSensor", m_attackSensor.get());
        visit("StateMachine", m_stateMachine.get());
        visit("HitReaction", m_hitReaction.get());
    }

    Component* Actor::Part(std::string_view name) const
    {
        Component* found = nullptr;
        ForEachPart([name, &found](std::string_view partName, Component& part) {
            if (name == partName)
            {
                found = &part;
            }
        });
        return found;
    }

    std::string_view Actor::PartName(const Component& part) const
    {
        std::string_view found;
        ForEachPart([&part, &found](std::string_view name, Component& candidate) {
            if (&part == &candidate)
            {
                found = name;
            }
        });
        return found;
    }

    Component* Actor::CreatePart(std::string_view name)
    {
        if (Component* existing = Part(name))
        {
            return existing;
        }
        Component* created = nullptr;
        if (name == "Model")
        {
            m_model = std::make_unique<Model>();
            created = m_model.get();
        }
        else if (name == "Animation")
        {
            m_animation = std::make_unique<Animation>();
            created = m_animation.get();
        }
        else if (name == "Shadow")
        {
            m_shadow = std::make_unique<Shadow>();
            created = m_shadow.get();
        }
        else if (name == "Collider")
        {
            m_collider = std::make_unique<CapsuleCollider>();
            created = m_collider.get();
        }
        else if (name == "Collision")
        {
            m_collision = std::make_unique<BoxCollider>();
            created = m_collision.get();
        }
        else if (name == "BodySensor")
        {
            m_bodySensor = std::make_unique<HitSensor>();
            created = m_bodySensor.get();
        }
        else if (name == "AttackSensor")
        {
            m_attackSensor = std::make_unique<HitSensor>();
            created = m_attackSensor.get();
        }
        else if (name == "StateMachine")
        {
            m_stateMachine = std::make_unique<StateMachineComponent>();
            created = m_stateMachine.get();
        }
        else if (name == "HitReaction")
        {
            m_hitReaction = std::make_unique<HitReaction>();
            created = m_hitReaction.get();
        }
        if (created != nullptr)
        {
            AttachFixedComponent(*created);
        }
        return created;
    }

    void Actor::SetCollisionPart(std::unique_ptr<Collider> collision)
    {
        if (m_collision != nullptr || collision == nullptr)
        {
            return;
        }
        m_collision = std::move(collision);
        AttachFixedComponent(*m_collision);
    }

    IStateMachine* Actor::GetStateMachine() noexcept
    {
        StateMachineComponent* component = m_stateMachine.get();
        if (component == nullptr)
        {
            return nullptr;
        }
        return &component->Machine();
    }

    const IStateMachine* Actor::GetStateMachine() const noexcept
    {
        const StateMachineComponent* component = m_stateMachine.get();
        if (component == nullptr)
        {
            return nullptr;
        }
        return &component->Machine();
    }

    Actor::~Actor() noexcept
    {
        DetachFromParent();
        for (Actor* child : m_children)
        {
            if (child == nullptr)
            {
                continue;
            }
            const bool wasActive = child->IsActiveInHierarchy();
            child->m_parent = nullptr;
            child->m_transform->SetParent(nullptr);
            // 消えていた親の下に居た子だけが根になって出る。出ていた子は登録をそのまま保つ
            if (!wasActive)
            {
                child->OnAppear();
            }
        }
    }

    bool Actor::IsActiveInHierarchy() const noexcept
    {
        // 1 つでも active が false なら効かない。循環は SetParent が作らせないので必ず root で止まる
        for (const Actor* node = this; node != nullptr; node = node->m_parent)
        {
            if (!node->IsAlive())
            {
                return false;
            }
        }
        return true;
    }

    void Actor::SetParent(Actor* parent) noexcept
    {
        if (parent == this || parent == m_parent)
        {
            return;
        }

        // 循環防止: parent の祖先に this がいたら拒否
        for (Actor* p = parent; p != nullptr; p = p->m_parent)
        {
            if (p == this)
            {
                return;
            }
        }

        const bool wasActive = IsActiveInHierarchy();
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
        // 登録し直すのは出ているかが変わった時だけ。毎回外して入れ直すと当たりの body の id が変わる
        // 姿勢の変化は呼び手の SyncPhysics が同じ id のまま揃える
        const bool isActive = IsActiveInHierarchy();
        if (wasActive && !isActive)
        {
            OnKill();
        }
        else if (!wasActive && isActive)
        {
            OnAppear();
        }
    }

    void Actor::DetachFromParent() noexcept
    {
        if (m_parent == nullptr)
        {
            return;
        }
        std::vector<Actor*>& siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        m_parent = nullptr;
    }

    void Actor::AttachFixedComponent(Component& component)
    {
        component.AttachOwner(this);
    }

    void Actor::OnStart()
    {
        // 部品の登録は部品の OnStart が済ませる。続けて OnAppear を呼ぶと、出る時に OnStart
        // を呼び直す部品が二重に登録する
        ForEachPart([](std::string_view, Component& part) { part.OnStart(); });
        if (!IsActiveInHierarchy())
        {
            OnKill();
            return;
        }
        if (Scene* scene = OwningScene())
        {
            scene->Objects().RegisterActor(this);
        }
    }

    void Actor::OnAppear()
    {
        Scene* scene = OwningScene();
        if (scene == nullptr || !IsActiveInHierarchy())
        {
            return;
        }
        scene->Objects().RegisterActor(this);
        ForEachPart([](std::string_view, Component& part) {
            if (part.IsActive())
            {
                part.OnAppear();
            }
        });
        for (Actor* child : m_children)
        {
            child->OnAppear();
        }
    }

    void Actor::OnKill() noexcept
    {
        Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->Objects().UnregisterActor(this);
        ForEachPart([](std::string_view, Component& part) { part.OnKill(); });
        for (Actor* child : m_children)
        {
            child->OnKill();
        }
    }

    void Actor::Update()
    {
        TickPart(m_model.get());
        TickPart(m_stateMachine.get());
        TickPart(m_hitReaction.get());
    }

    void Actor::OnPrePhysicsStep() {}

    void Actor::OnPostPhysicsStep() {}

    void Actor::PrepareRender()
    {
        TickPart(m_animation.get());
    }

    void Actor::OnUpdate()
    {
        Update();
    }

    void Actor::OnEndPlay()
    {
        Kill();
        ForEachPart([](std::string_view, Component& part) { part.OnEndPlay(); });
    }

} // namespace NS::Obj
