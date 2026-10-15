#include "NSlib/Object/Actor.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/SubObjects/Animation.h"
#include "NSlib/Object/SubObjects/Collision.h"
#include "NSlib/Object/SubObjects/EffectKeeper.h"
#include "NSlib/Object/SubObjects/HitReaction.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/SubObjects/Shadow.h"
#include "NSlib/Windows/Clock.h"

#include <algorithm>
#include <cmath>

namespace NS::Obj
{

    NS_REFLECT_BEGIN_IMPL(Actor, Object)
    NS_REFLECT_SUBOBJ(m_transform, "Transform")
    NS_REFLECT_SUBOBJ(m_model, "Model")
    NS_REFLECT_SUBOBJ(m_animation, "Animation")
    NS_REFLECT_SUBOBJ(m_shadow, "Shadow")
    NS_REFLECT_SUBOBJ(m_collision, "Collision")
    NS_REFLECT_SUBOBJ(m_bodySensor, "BodySensor")
    NS_REFLECT_SUBOBJ(m_attackSensor, "AttackSensor")
    NS_REFLECT_SUBOBJ(m_hitReaction, "HitReaction")
    NS_REFLECT_SUBOBJ(m_effectKeeper, "Effect")
    NS_REFLECT_END_IMPL()

    Actor::Actor() noexcept
    {
        // Init 前にも姿勢と親子関係を使うため、Transform はここで用意する
        (void)CreateSubObj(m_transform);
        Appear();
    }

    Transform& Actor::Root() noexcept
    {
        return *m_transform;
    }

    const Transform& Actor::Root() const noexcept
    {
        return *m_transform;
    }

    namespace
    {
        void VisitReflectedSubObjects(const Object& owner,
                                      const ReflectionInfo* info,
                                      const Actor::SubObjFieldVisitor& visit)
        {
            if (info == nullptr)
            {
                return;
            }
            VisitReflectedSubObjects(owner, info->base, visit);
            for (std::size_t i = 0; i < info->subObjectCount; ++i)
            {
                const SubObjectDesc& field = info->subObjects[i];
                if (SubObject* part = field.get(&owner))
                {
                    visit(field, part);
                }
            }
        }

        // ForEachSubObj の std::function を介さず、登録を直接たどる
        template <class TMatch> const SubObjectDesc* FindCreatedSubObj(const Actor& owner, TMatch match)
        {
            for (const ReflectionInfo* info = owner.GetReflection(); info != nullptr; info = info->base)
            {
                for (std::size_t i = 0; i < info->subObjectCount; ++i)
                {
                    const SubObjectDesc& field = info->subObjects[i];
                    const SubObject* part = field.get(&owner);
                    if (part != nullptr && match(field, *part))
                    {
                        return &field;
                    }
                }
            }
            return nullptr;
        }
    } // namespace

    void Actor::ForEachSubObj(const SubObjVisitor& visit) const
    {
        ForEachSubObj([&](const SubObjectDesc&, SubObject* part) { visit(part); });
    }

    void Actor::ForEachSubObj(const SubObjFieldVisitor& visit) const
    {
        VisitReflectedSubObjects(*this, GetReflection(), visit);
    }

    const SubObjectDesc* Actor::FindSubObjMember(const void* member) const
    {
        for (const ReflectionInfo* info = GetReflection(); info != nullptr; info = info->base)
        {
            for (std::size_t i = 0; i < info->subObjectCount; ++i)
            {
                const SubObjectDesc& field = info->subObjects[i];
                if (field.member(this) == member)
                {
                    return &field;
                }
            }
        }
        return nullptr;
    }

    void Actor::ReportInvalidSubObj(const SubObjectDesc* field) const
    {
        if (field == nullptr)
        {
            NS_LOG_ERROR(Scene, "{} の部品の所有メンバーがリフレクションに未登録", ClassName());
            return;
        }
        NS_LOG_ERROR(Scene, "{} の部品 {} は作成済みか、保存キーが使用済み", ClassName(), field->key);
    }

    std::string_view Actor::SubObjKey(const SubObject& part) const
    {
        const SubObjectDesc* field = FindCreatedSubObj(
            *this, [&part](const SubObjectDesc&, const SubObject& candidate) { return &candidate == &part; });
        if (field == nullptr)
        {
            return {};
        }
        return field->key;
    }

    SubObject* Actor::FindSubObj(std::string_view key) const
    {
        const SubObjectDesc* field = FindCreatedSubObj(
            *this, [key](const SubObjectDesc& candidate, const SubObject&) { return candidate.key == key; });
        if (field == nullptr)
        {
            return nullptr;
        }
        return field->get(this);
    }

    IStateMachine* Actor::GetStates() noexcept
    {
        return m_stateMachine.get();
    }

    const IStateMachine* Actor::GetStates() const noexcept
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

    float Actor::BodyDelta() const noexcept
    {
        return NS::OS::FrameTimer::FixedDelta() * m_bodyTimeScale;
    }

    void Actor::Appear()
    {
        NS_ASSERT(Scene, !IsPendingDestroy(), "Destroy した個体を Appear で出そうとしている: {}", m_name);
        if (m_presence != Presence::Disappeared)
        {
            return;
        }
        m_presence = Presence::Appeared;
        OnAppear();
    }

    void Actor::Disappear() noexcept
    {
        if (m_presence != Presence::Appeared)
        {
            return;
        }
        m_presence = Presence::Disappeared;
        OnDisappear();
    }

    void Actor::Destroy() noexcept
    {
        Disappear();
        m_presence = Presence::PendingDestroy;
    }

    CameraManager* Actor::GetCameraManager() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetCameraManager();
    }

    SceneSystemHolder* Actor::GetSceneSystemHolder() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetSceneSystemHolder();
    }

    NS::Phys::PhysicsScene* Actor::GetPhysicsScene() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetPhysicsScene();
    }

    NS::Vector3 Actor::GravityDirection() const noexcept
    {
        const Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return NS::Vector3{0.0f, -1.0f, 0.0f};
        }
        return scene->GravityDirection();
    }

    void Actor::AddGravity(NS::Vector3& velocity, float strength, float dt) const noexcept
    {
        if (!std::isfinite(strength) || !std::isfinite(dt) || !(dt > 0.0f))
        {
            return;
        }
        velocity += GravityDirection() * (strength * dt);
    }

    bool Actor::SetBodyTimeScale(float scale) noexcept
    {
        if (!std::isfinite(scale) || !(scale > 0.0f))
        {
            NS_LOG_ERROR(Scene,
                         "Actor::SetBodyTimeScale: 体の倍率 {} は正の有限値でない。前の {} を残す",
                         scale,
                         m_bodyTimeScale);
            return false;
        }
        m_bodyTimeScale = scale;
        return true;
    }

    void Actor::StepStateMachine()
    {
        if (m_stateMachine != nullptr)
        {
            m_stateMachine->Step(BodyDelta());
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
            child->Root().SetParent(nullptr);
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
            if (!node->IsAppeared())
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
            parentRoot = &parent->Root();
        }
        Root().SetParent(parentRoot);
        // 登録し直すのは出ているかが変わった時だけ。毎回外して入れ直すと当たりの body の id が変わる
        // 姿勢の変化は呼び手の SyncPhysics が同じ id のまま揃える
        const bool isActive = IsActiveInHierarchy();
        if (wasActive && !isActive)
        {
            OnDisappear();
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
        CallIfPresent(m_model, &Model::Register);
        CallIfPresent(m_animation, &Animation::BindToModel);
        CallIfPresent(m_shadow, &Shadow::Register);
        CallIfPresent(m_bodySensor, &HitSensor::Register);
        CallIfPresent(m_attackSensor, &HitSensor::Register);
        CallIfPresent(m_hitReaction, &HitReaction::CreateDirectors);
        ForEachSubObj([&](SubObject* subObject) { subObject->OnStart(); });
        if (!IsActiveInHierarchy())
        {
            OnDisappear();
        }
    }

    void Actor::OnAppear()
    {
        // 部品が描画・当たり・センサーのシーンの仕組みへ登録するので、シーンに付いていない間は何もしない
        if (OwningScene() == nullptr || !IsActiveInHierarchy())
        {
            return;
        }
        CallIfActive(m_model, &Model::Register);
        CallIfActive(m_shadow, &Shadow::Register);
        CallIfActive(m_collision, &Collision::SyncToPhysics);
        CallIfActive(m_bodySensor, &HitSensor::Register);
        CallIfActive(m_attackSensor, &HitSensor::Register);
        ForEachSubObj([&](SubObject* subObject) {
            if (subObject->IsActive())
            {
                subObject->OnAppear();
            }
        });
        for (Actor* child : m_children)
        {
            child->OnAppear();
        }
    }

    void Actor::OnDisappear() noexcept
    {
        if (OwningScene() == nullptr)
        {
            return;
        }
        CallIfPresent(m_model, &Model::Unregister);
        CallIfPresent(m_shadow, &Shadow::Unregister);
        CallIfPresent(m_collision, &Collision::Unregister);
        CallIfPresent(m_bodySensor, &HitSensor::Unregister);
        CallIfPresent(m_attackSensor, &HitSensor::Unregister);
        CallIfPresent(m_hitReaction, &HitReaction::StopAll);
        ForEachSubObj([&](SubObject* subObject) { subObject->OnDisappear(); });
        for (Actor* child : m_children)
        {
            child->OnDisappear();
        }
    }

    void Actor::Update()
    {
        CallIfActive(m_effectKeeper, &EffectKeeper::BeginStep);
        ObserveStep();
        DecideStep();
        StateStep();
        BodyStep();
        VisualStep();
    }

    void Actor::SnapshotForInterpolation() noexcept
    {
        Root().Snapshot();
        CallIfPresent(m_model, &Model::Snapshot);
    }

    void Actor::StateStep()
    {
        StepStateMachine();
    }

    void Actor::PrepareRender()
    {
        TickSubObj(m_animation.get());
    }

    void Actor::OnEndPlay()
    {
        Disappear();
        CallIfPresent(m_model, &Model::Unregister);
        CallIfPresent(m_shadow, &Shadow::Unregister);
        CallIfPresent(m_collision, &Collision::Unregister);
        CallIfPresent(m_bodySensor, &HitSensor::Unregister);
        CallIfPresent(m_attackSensor, &HitSensor::Unregister);
        CallIfPresent(m_hitReaction, &HitReaction::StopAll);
        ForEachSubObj([&](SubObject* subObject) { subObject->OnEndPlay(); });
    }

} // namespace NS::Obj
