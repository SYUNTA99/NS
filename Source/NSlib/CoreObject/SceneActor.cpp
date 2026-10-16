#include "NSlib/CoreObject/SceneActor.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/AssetManager.h"
#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/CoreObject/Reflection/AssetRef.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedSubObjs.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/SubObjects/Animation.h"
#include "NSlib/CoreObject/SubObjects/Collision.h"
#include "NSlib/CoreObject/SubObjects/EffectKeeper.h"
#include "NSlib/CoreObject/SubObjects/HitReaction.h"
#include "NSlib/CoreObject/SubObjects/HitSensor.h"
#include "NSlib/CoreObject/SubObjects/MeshCollision.h"
#include "NSlib/CoreObject/SubObjects/Model.h"
#include "NSlib/CoreObject/SubObjects/Shadow.h"
#include "NSlib/Windows/Clock.h"

#include <algorithm>
#include <cmath>

namespace NS
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
    NS_REFLECT_SUBOBJ(m_stateMachine, "StateMachine")
    NS_REFLECT_END_IMPL()

    SceneActor::SceneActor() noexcept
    {
        // Init 前にも姿勢を使うため、Transform はここで用意する
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

    const SubObjectDesc* SceneActor::FindReflectedMember(const void* member) const
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

    bool SceneActor::IsSubObjKeyCreated(std::string_view key) const
    {
        bool created = false;
        ForEachReflectedSubObj(*this, [&](const SubObjectDesc& field, const Object*) {
            if (field.key == key)
            {
                created = true;
            }
        });
        return created;
    }

    IStateMachine* SceneActor::GetStates() noexcept
    {
        if (m_stateMachine == nullptr)
        {
            return nullptr;
        }
        return m_stateMachine->Machine();
    }

    const IStateMachine* SceneActor::GetStates() const noexcept
    {
        if (m_stateMachine == nullptr)
        {
            return nullptr;
        }
        return m_stateMachine->Machine();
    }

    float SceneActor::BodyDelta() const noexcept
    {
        return OS::FrameTimer::FixedDelta() * m_bodyTimeScale;
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

    Phys::PhysicsScene* SceneActor::GetPhysicsScene() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetPhysicsScene();
    }

    Vector3 SceneActor::GravityDirection() const noexcept
    {
        const Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return Vector3{0.0f, -1.0f, 0.0f};
        }
        return scene->GravityDirection();
    }

    void SceneActor::AddGravity(Vector3& velocity, float strength, float dt) const noexcept
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

    SceneActor::~SceneActor() noexcept = default;

    void SceneActor::Init(const ActorInitInfo& info)
    {
        OnInit();
        ApplyArchetype(*this);
        if (info.placement != nullptr)
        {
            ApplyObjectSubObjs(*this, *info.placement);
        }
        // 素材の参照は欄にある。欄を書き終えてから読む
        ReloadAssets();
    }

    void SceneActor::ReloadAssets()
    {
        LoadAssetRefs(*this);
        OnAssetsLoaded();
    }

    void SceneActor::OnAssetsLoaded()
    {
        // 読んだ素材から作る物。骨付きのメッシュを Model に差してから、Model のメッシュの名前で当たりを作る
        if (m_animation)
        {
            m_animation->UseModel();
        }
        if (MeshCollision* meshCollision = Cast<MeshCollision>(m_collision.get()))
        {
            if (m_model)
            {
                meshCollision->SetShape(AssetManager::Get().LoadMeshShape(m_model->MeshRef()));
            }
            else
            {
                NS_LOG_WARN(Scene, "{} は Model が無いので、メッシュの当たりは空のまま", ClassName());
            }
        }
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
        if (!IsAppeared())
        {
            OnDisappear();
        }
    }

    void SceneActor::OnAppear()
    {
        // 部品が描画・当たり・センサーのシーンの仕組みへ登録するので、シーンに付いていない間は何もしない
        if (OwningScene() == nullptr || !IsAppeared())
        {
            return;
        }
        // clang-format off
        if (m_model) { m_model->Register(); }
        if (m_shadow) { m_shadow->Register(); }
        if (m_collision) { m_collision->SyncToPhysics(); }
        if (m_bodySensor) { m_bodySensor->Register(); }
        if (m_attackSensor) { m_attackSensor->Register(); }
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
    }

    void SceneActor::Update()
    {
        // clang-format off
        if (m_effectKeeper) { m_effectKeeper->BeginStep(); }
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
        // clang-format off
        if (m_stateMachine) { m_stateMachine->Update(); }
        // clang-format on
    }

    void SceneActor::PrepareRender()
    {
        // clang-format off
        if (m_animation) { m_animation->Update(); }
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

} // namespace NS
