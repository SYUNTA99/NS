#include "NSlib/Object/Actor.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/SubObjects/Animation.h"
#include "NSlib/Object/SubObjects/BoxCollision.h"
#include "NSlib/Object/SubObjects/HitReaction.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/SubObjects/Shadow.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Windows/Clock.h"

#include <algorithm>
#include <iterator>

namespace NS::Obj
{

    Actor::Actor() noexcept
    {
        (void)AdoptSubObj(std::make_unique<TransformSubObject>(), k_TransformSubObjName, m_rootSubObj);
        m_transform = &m_rootSubObj->Root();
        Appear();
    }

    void Actor::Init()
    {
        if (m_initialized)
        {
            return;
        }
        m_initialized = true;
        OnInit();
    }

    std::size_t Actor::OrderRank(const SubObject* subObject) const noexcept
    {
        const SubObject* const ranked[] = {
            m_rootSubObj, m_model, m_animation, m_shadow, m_collision, m_bodySensor, m_attackSensor, m_hitReaction};
        return static_cast<std::size_t>(std::find(std::begin(ranked), std::end(ranked), subObject) -
                                        std::begin(ranked));
    }

    void Actor::AdoptSubObjBase(std::unique_ptr<SubObject> subObject, std::string_view name)
    {
        subObject->SetName(name);
        subObject->AttachOwner(this);
        const std::size_t rank = OrderRank(subObject.get());
        const std::vector<SubObject*>::iterator at =
            std::find_if(m_subObjOrder.begin(), m_subObjOrder.end(), [this, rank](const SubObject* placed) {
                return OrderRank(placed) > rank;
            });
        m_subObjOrder.insert(at, subObject.get());
        m_subObjects.push_back(std::move(subObject));
    }

    void Actor::ReportTakenName(std::string_view name) const
    {
        NS_LOG_ERROR(Scene, "Actor::CreateSubObj: 部品名 {} は {} が使っている。2 回目は作らない", name, ClassName());
    }

    SubObject* Actor::FindSubObj(std::string_view name) const
    {
        for (SubObject* subObject : m_subObjOrder)
        {
            if (subObject->Name() == name)
            {
                return subObject;
            }
        }
        return nullptr;
    }

    SubObject* Actor::CreateSubObj(std::string_view name)
    {
        if (SubObject* existing = FindSubObj(name))
        {
            return existing;
        }
        if (name == "Model")
        {
            return CreateSubObj<Model>(ModelSlot());
        }
        if (name == "Animation")
        {
            return CreateSubObj<Animation>(AnimationSlot());
        }
        if (name == "Shadow")
        {
            return CreateSubObj<Shadow>(ShadowSlot());
        }
        if (name == "Collision")
        {
            return CreateSubObj<BoxCollision>(CollisionSlot());
        }
        if (name == "BodySensor")
        {
            return CreateSubObj<ShapeHitSensor>(BodySensorSlot());
        }
        if (name == "AttackSensor")
        {
            return CreateSubObj<ShapeHitSensor>(AttackSensorSlot());
        }
        if (name == "HitReaction")
        {
            return CreateSubObj<HitReaction>(HitReactionSlot());
        }
        return nullptr;
    }

    IStateMachine* Actor::GetStateMachine() noexcept
    {
        return m_stateMachine.get();
    }

    const IStateMachine* Actor::GetStateMachine() const noexcept
    {
        return m_stateMachine.get();
    }

    bool Actor::AdoptStateMachine(std::unique_ptr<IStateMachine> machine)
    {
        if (m_stateMachine != nullptr)
        {
            NS_LOG_ERROR(Scene, "Actor::BuildStateMachine: 状態機械は 1 体に 1 つ。2 回目は組まずに今の機械を残す");
            return false;
        }
        m_stateMachine = std::move(machine);
        return true;
    }

    void Actor::StepStateMachine()
    {
        if (m_stateMachine != nullptr)
        {
            m_stateMachine->Step(NS::OS::FrameTimer::FixedDelta());
        }
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
        m_subObjOrder.clear();
        // 作った順の逆に壊す。clear は壊す順を決めていない
        while (!m_subObjects.empty())
        {
            m_subObjects.pop_back();
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

    void Actor::OnStart()
    {
        // 部品の登録は部品の OnStart が済ませる。続けて OnAppear を呼ぶと、出る時に OnStart
        // を呼び直す部品が二重に登録する
        for (SubObject* subObject : m_subObjOrder)
        {
            subObject->OnStart();
        }
        if (!IsActiveInHierarchy())
        {
            OnKill();
        }
    }

    void Actor::OnAppear()
    {
        // 部品が描画・当たり・センサーのシーンの仕組みへ登録するので、シーンに付いていない間は何もしない
        if (OwningScene() == nullptr || !IsActiveInHierarchy())
        {
            return;
        }
        for (SubObject* subObject : m_subObjOrder)
        {
            if (subObject->IsActive())
            {
                subObject->OnAppear();
            }
        }
        for (Actor* child : m_children)
        {
            child->OnAppear();
        }
    }

    void Actor::OnKill() noexcept
    {
        if (OwningScene() == nullptr)
        {
            return;
        }
        for (SubObject* subObject : m_subObjOrder)
        {
            subObject->OnKill();
        }
        for (Actor* child : m_children)
        {
            child->OnKill();
        }
    }

    void Actor::Update()
    {
        ObserveStep();
        DecideStep();
        StateStep();
        BodyStep();
        VisualStep();
    }

    void Actor::SnapshotForInterpolation() noexcept
    {
        m_transform->Snapshot();
        if (m_model != nullptr)
        {
            m_model->Snapshot();
        }
    }

    void Actor::StateStep()
    {
        StepStateMachine();
    }

    void Actor::VisualStep()
    {
        TickSubObj(m_hitReaction);
    }

    void Actor::PrepareRender()
    {
        TickSubObj(m_animation);
    }

    void Actor::OnEndPlay()
    {
        Kill();
        for (SubObject* subObject : m_subObjOrder)
        {
            subObject->OnEndPlay();
        }
    }

} // namespace NS::Obj
