#include "NSlib/Object/SceneActor.h"

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

    NS_REFLECT_BEGIN_IMPL(SceneActor, Object)
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

    SceneActor::SceneActor() noexcept
    {
        // Init 前にも姿勢と親子関係を使うため、Transform はここで用意する
        CreateSubObj(m_transform);
        Appear();
    }

    Transform& SceneActor::Root() noexcept
    {
        return *m_transform;
    }

    const Transform& SceneActor::Root() const noexcept
    {
        return *m_transform;
    }

    namespace
    {
        // ForEachReflectedSubObj の std::function を介さず、登録を直接たどる
        template <class TMatch> const SubObjectDesc* FindCreatedSubObj(const SceneActor& owner, TMatch match)
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

    const SubObjectDesc* SceneActor::FindSubObjMember(const void* member) const
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

    void SceneActor::ReportInvalidSubObj(const SubObjectDesc* field) const
    {
        if (field == nullptr)
        {
            NS_LOG_ERROR(Scene, "{} の部品の所有メンバーがリフレクションに未登録", ClassName());
            return;
        }
        NS_LOG_ERROR(Scene, "{} の部品 {} は作成済みか、保存キーが使用済み", ClassName(), field->key);
    }

    std::string_view SceneActor::SubObjKey(const SubObject& part) const
    {
        const SubObjectDesc* field = FindCreatedSubObj(
            *this, [&part](const SubObjectDesc&, const SubObject& candidate) { return &candidate == &part; });
        if (field == nullptr)
        {
            return {};
        }
        return field->key;
    }

    SubObject* SceneActor::FindSubObj(std::string_view key) const
    {
        const SubObjectDesc* field = FindCreatedSubObj(
            *this, [key](const SubObjectDesc& candidate, const SubObject&) { return candidate.key == key; });
        if (field == nullptr)
        {
            return nullptr;
        }
        return field->get(this);
    }

    IStateMachine* SceneActor::GetStates() noexcept
    {
        return m_stateMachine.get();
    }

    const IStateMachine* SceneActor::GetStates() const noexcept
    {
        return m_stateMachine.get();
    }

    bool SceneActor::AdoptStateMachine(std::unique_ptr<IStateMachine> machine)
    {
        if (m_stateMachine != nullptr)
        {
            NS_LOG_ERROR(Scene, "SceneActor::BuildStateMachine: 状態機械は 1 体に 1 つ。2 回目は組まずに今の機械を残す");
            return false;
        }
        m_stateMachine = std::move(machine);
        return true;
    }

    float SceneActor::BodyDelta() const noexcept
    {
        return NS::OS::FrameTimer::FixedDelta() * m_bodyTimeScale;
    }

    void SceneActor::Appear()
    {
        NS_ASSERT(Scene, !IsPendingDestroy(), "Destroy した個体を Appear で出そうとしている: {}", m_name);
        if (m_presence != Presence::Disappeared)
        {
            return;
        }
        m_presence = Presence::Appeared;
        OnAppear();
    }

    void SceneActor::Disappear() noexcept
    {
        if (m_presence != Presence::Appeared)
        {
            return;
        }
        m_presence = Presence::Disappeared;
        OnDisappear();
    }

    void SceneActor::Destroy() noexcept
    {
        Disappear();
        m_presence = Presence::PendingDestroy;
    }

    CameraManager* SceneActor::GetCameraManager() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetCameraManager();
    }

    SceneSystemHolder* SceneActor::GetSceneSystemHolder() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetSceneSystemHolder();
    }

    NS::Phys::PhysicsScene* SceneActor::GetPhysicsScene() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetPhysicsScene();
    }

    NS::Vector3 SceneActor::GravityDirection() const noexcept
    {
        const Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return NS::Vector3{0.0f, -1.0f, 0.0f};
        }
        return scene->GravityDirection();
    }

    void SceneActor::AddGravity(NS::Vector3& velocity, float strength, float dt) const noexcept
    {
        if (!std::isfinite(strength) || !std::isfinite(dt) || !(dt > 0.0f))
        {
            return;
        }
        velocity += GravityDirection() * (strength * dt);
    }

    bool SceneActor::SetBodyTimeScale(float scale) noexcept
    {
        if (!std::isfinite(scale) || !(scale > 0.0f))
        {
            NS_LOG_ERROR(Scene,
                         "SceneActor::SetBodyTimeScale: 体の倍率 {} は正の有限値でない。前の {} を残す",
                         scale,
                         m_bodyTimeScale);
            return false;
        }
        m_bodyTimeScale = scale;
        return true;
    }

    void SceneActor::StepStateMachine()
    {
        if (m_stateMachine != nullptr)
        {
            m_stateMachine->Step(BodyDelta());
        }
    }

    SceneActor::~SceneActor() noexcept
    {
        DetachFromParent();
        for (SceneActor* child : m_children)
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

    bool SceneActor::IsActiveInHierarchy() const noexcept
    {
        // 1 つでも active が false なら効かない。循環は SetParent が作らせないので必ず root で止まる
        for (const SceneActor* node = this; node != nullptr; node = node->m_parent)
        {
            if (!node->IsAppeared())
            {
                return false;
            }
        }
        return true;
    }

    void SceneActor::SetParent(SceneActor* parent) noexcept
    {
        if (parent == this || parent == m_parent)
        {
            return;
        }

        // 循環防止: parent の祖先に this がいたら拒否
        for (SceneActor* p = parent; p != nullptr; p = p->m_parent)
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

    void SceneActor::DetachFromParent() noexcept
    {
        if (m_parent == nullptr)
        {
            return;
        }
        std::vector<SceneActor*>& siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        m_parent = nullptr;
    }

    void SceneActor::OnStart()
    {
        // 部品の登録はここで済ませる。続けて OnAppear を呼ぶと二重に登録する
        // clang-format off
        if (m_model) { m_model->Register(); }
        if (m_animation) { m_animation->BindToModel(); }
        if (m_shadow) { m_shadow->Register(); }
        if (m_bodySensor) { m_bodySensor->Register(); }
        if (m_attackSensor) { m_attackSensor->Register(); }
        if (m_hitReaction) { m_hitReaction->CreateDirectors(); }
        // clang-format on
        if (!IsActiveInHierarchy())
        {
            OnDisappear();
        }
    }

    void SceneActor::OnAppear()
    {
        // 部品が描画・当たり・センサーのシーンの仕組みへ登録するので、シーンに付いていない間は何もしない
        if (OwningScene() == nullptr || !IsActiveInHierarchy())
        {
            return;
        }
        // clang-format off
        if (m_model && m_model->IsActive()) { m_model->Register(); }
        if (m_shadow && m_shadow->IsActive()) { m_shadow->Register(); }
        if (m_collision && m_collision->IsActive()) { m_collision->SyncToPhysics(); }
        if (m_bodySensor && m_bodySensor->IsActive()) { m_bodySensor->Register(); }
        if (m_attackSensor && m_attackSensor->IsActive()) { m_attackSensor->Register(); }
        // clang-format on
        for (SceneActor* child : m_children)
        {
            child->OnAppear();
        }
    }

    void SceneActor::OnDisappear() noexcept
    {
        if (OwningScene() == nullptr)
        {
            return;
        }
        // clang-format off
        if (m_model) { m_model->Unregister(); }
        if (m_shadow) { m_shadow->Unregister(); }
        if (m_collision) { m_collision->Unregister(); }
        if (m_bodySensor) { m_bodySensor->Unregister(); }
        if (m_attackSensor) { m_attackSensor->Unregister(); }
        if (m_hitReaction) { m_hitReaction->StopAll(); }
        // clang-format on
        for (SceneActor* child : m_children)
        {
            child->OnDisappear();
        }
    }

    void SceneActor::Update()
    {
        // clang-format off
        if (m_effectKeeper && m_effectKeeper->IsActive()) { m_effectKeeper->BeginStep(); }
        // clang-format on
        ObserveStep();
        DecideStep();
        StateStep();
        BodyStep();
        VisualStep();
    }

    void SceneActor::SnapshotForInterpolation() noexcept
    {
        Root().Snapshot();
        // clang-format off
        if (m_model) { m_model->Snapshot(); }
        // clang-format on
    }

    void SceneActor::StateStep()
    {
        StepStateMachine();
    }

    void SceneActor::PrepareRender()
    {
        // clang-format off
        if (m_animation && m_animation->IsActive()) { m_animation->Advance(); }
        // clang-format on
    }

    void SceneActor::OnEndPlay()
    {
        Disappear();
        // clang-format off
        if (m_model) { m_model->Unregister(); }
        if (m_shadow) { m_shadow->Unregister(); }
        if (m_collision) { m_collision->Unregister(); }
        if (m_bodySensor) { m_bodySensor->Unregister(); }
        if (m_attackSensor) { m_attackSensor->Unregister(); }
        if (m_hitReaction) { m_hitReaction->StopAll(); }
        // clang-format on
    }

} // namespace NS::Obj
