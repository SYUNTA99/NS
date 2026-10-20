#include "NSlib/CoreObject/SceneActor.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/ClassObjs/Collision.h"
#include "NSlib/CoreObject/ClassObjs/EffectKeeper.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Phys/PhysicsScene.h"
#include "NSlib/Windows/Clock.h"

#include <algorithm>
#include <cmath>

namespace NS
{

    NS_REFLECT_BEGIN_IMPL(SceneActor, Object)
    NS_CLASSOBJ(m_transform, "Transform")
    NS_CLASSOBJ(m_model, "Model")
    NS_CLASSOBJ(m_animation, "Animation")
    NS_CLASSOBJ(m_shadow, "Shadow")
    NS_CLASSOBJ(m_collision, "Collision")
    NS_CLASSOBJ(m_bodySensor, "BodySensor")
    NS_CLASSOBJ(m_attackSensor, "AttackSensor")
    NS_CLASSOBJ(m_hitReaction, "HitReaction")
    NS_CLASSOBJ(m_effectKeeper, "Effect")
    NS_CLASSOBJ(m_stateMachine, "StateMachine")
    NS_REFLECT_END_IMPL()

    SceneActor::SceneActor() noexcept
    {
        // Init 前にも姿勢を使うため、Transform はここで用意する
        CreateClassObj(m_transform);
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

    const ClassObjInfo* SceneActor::FindReflectedMember(const void* member) const
    {
        for (const ReflectionInfo* info = GetReflection(); info != nullptr; info = info->base)
        {
            for (std::size_t i = 0; i < info->classObjCount; ++i)
            {
                const ClassObjInfo& field = info->classObjs[i];
                if (field.member(this) == member)
                {
                    return &field;
                }
            }
        }
        return nullptr;
    }

    void SceneActor::ReportInvalidClassObj(const ClassObjInfo* field) const
    {
        if (field == nullptr)
        {
            NS_LOG_ERROR(Scene, "{} の部品の所有メンバーがリフレクションに未登録", ClassName());
            return;
        }
        NS_LOG_ERROR(Scene, "{} の部品 {} は作成済みか、保存キーが使用済み", ClassName(), field->key);
    }

    bool SceneActor::IsClassObjKeyCreated(std::string_view key) const
    {
        bool created = false;
        ForEachReflectedClassObj(*this, [&](const ClassObjInfo& field, const Object*) {
            if (field.key == key)
            {
                created = true;
            }
        });
        return created;
    }

    IStateMachine* SceneActor::GetStateMachine() noexcept
    {
        if (m_stateMachine == nullptr)
        {
            return nullptr;
        }
        return m_stateMachine->Machine();
    }

    const IStateMachine* SceneActor::GetStateMachine() const noexcept
    {
        if (m_stateMachine == nullptr)
        {
            return nullptr;
        }
        return m_stateMachine->Machine();
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
        return Phys::k_GravityDirection;
    }

    void SceneActor::AddGravity(Vector3& velocity, float strength, float dt) const noexcept
    {
        if (!std::isfinite(strength) || !std::isfinite(dt) || !(dt > 0.0f))
        {
            return;
        }
        velocity += GravityDirection() * (strength * dt);
    }

    float GetFrameRate(const SceneActor* actor) noexcept
    {
        NS_ASSERT(Scene, actor != nullptr, "GetFrameRate: アクターが null");
        return actor->m_frameRate;
    }

    bool SetFrameRate(SceneActor* actor, float rate) noexcept
    {
        NS_ASSERT(Scene, actor != nullptr, "SetFrameRate: アクターが null");
        if (!std::isfinite(rate) || !(rate > 0.0f))
        {
            NS_LOG_ERROR(
                Scene, "SetFrameRate: 体の倍率 {} は正の有限値でない。前の {} を残す", rate, actor->m_frameRate);
            return false;
        }
        actor->m_frameRate = rate;
        return true;
    }

    float GetFrameDelta(const SceneActor* actor) noexcept
    {
        return OS::FrameTimer::FixedDelta() * GetFrameRate(actor);
    }

    SceneActor::~SceneActor() noexcept = default;

    void SceneActor::Init(const ActorInitInfo* info)
    {
        OnInit();
        ApplyActorFile(*this);
        if (info != nullptr && info->placement != nullptr)
        {
            ApplyObjectClassObjs(*this, *info->placement);
        }
        // 素材の参照は欄にある。欄を書き終えてから読む
        ReloadAssets();
    }

    void SceneActor::ReloadAssets()
    {
        LoadAssetFields(*this);
        // 差したメッシュを残すと、骨付きのモデルの欄を空にした後も前のメッシュを描く。差し直すのは OnAssetsLoaded
        if (m_model)
        {
            m_model->SetMesh(nullptr);
        }
        OnAssetsLoaded();
        // マテリアルの無いメッシュの描き方は描くメッシュで決まるので、OnAssetsLoaded がメッシュを差した後に選ぶ
        if (m_model)
        {
            m_model->ChooseDrawnMaterial();
        }
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
        NS_ASSERT(Scene,
                  Phase() != UpdatePhase::Input && Phase() != UpdatePhase::RenderPrep,
                  "{} の段が、全部の Actor を回す段になっている。Update が呼ばれない",
                  ClassName());
        // シーンの仕組みへのつなぎと外しは OnAppear と OnDisappear の 1 組だけが持つ。ここは出ている物をつなぐだけ
        OnAppear();
        // 作る時に 1 回だけの物
        // clang-format off
        if (m_animation) { m_animation->BindToModel(); }
        if (m_hitReaction) { m_hitReaction->CreateDirectors(); }
        // clang-format on
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
        // clang-format on
        // 出た所から描き始める。前の値が原点や消えた所のままだと、そこから補間して見える
        SnapshotForInterpolation();
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
        // 置く側の当たりは体を動かした後の姿へ揃える。姿も形も変わっていなければ Collision は何もしない
        // clang-format off
        if (m_collision) { m_collision->SyncToPhysics(); }
        // clang-format on
        // 揺れと震えは体を動かした後の根で書き、VisualStep の残像が書いた後の姿を写し取る
        // clang-format off
        if (m_model) { m_model->AdvanceDraw(); }
        // clang-format on
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
        // 出ている物は OnDisappear で外れる。消えている物は消えた時に外れている
        Disappear();
    }

} // namespace NS
