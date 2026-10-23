#include "NSlib/CoreObject/SceneActor.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"
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
#include "NSlib/CoreObject/Reflection/ActorBuilder.h"
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
    NS_CLASSOBJ(m_actionTimeline, "ActionTimeline")
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

    void SceneActor::InitWithCodeValues()
    {
        m_actorFileName = ClassName();
        OnInit();
    }

    bool SceneActor::IsSceneOverride(std::string_view part, std::string_view field) const
    {
        return m_sceneOverrides.contains({std::string{part}, std::string{field}});
    }

    void SceneActor::AddSceneOverride(std::string_view part, std::string_view field)
    {
        m_sceneOverrides.emplace(std::string{part}, std::string{field});
    }

    void SceneActor::RemoveSceneOverride(std::string_view part, std::string_view field)
    {
        m_sceneOverrides.erase({std::string{part}, std::string{field}});
    }

    bool SceneActor::Init(const ActorInitInfo* info)
    {
        m_actorFileName = ClassName();
        if (info != nullptr && !info->actorFile.empty())
        {
            m_actorFileName = std::string{info->actorFile};
        }
        // 部品は作る時にシーンの仕組みを受け取るので、OnInit より前にシーンを決める。書くのはここだけ
        if (info != nullptr)
        {
            m_scene = info->scene;
        }
        OnInit();
        ApplyActorFile(*this);
        // 束と動作は .actor だけに書き、配置物では上書きしない
        // clang-format off
        if (m_hitReaction) { m_hitReaction->ReloadSets(); }
        if (m_actionTimeline) { m_actionTimeline->ReloadActions(); }
        // clang-format on
        if (info != nullptr && info->placement != nullptr)
        {
            // シーンの上書きは書いてよい項目だけを持つ。書いてよくない項目を捨てて作ると、保存で黙って消える
            if (!MergeLayer(nlohmann::json::object(), *info->placement, FieldDestination::Scene, *this).has_value())
            {
                return false;
            }
            ApplyActorClassObjs(*this, *info->placement);
            ReadSceneOverrides(*this, *info->placement);
        }
        // 素材の参照は欄にある。欄を書き終えてから読む
        ReloadAssets();
        return true;
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
        if (m_actionTimeline) { m_actionTimeline->Stop(); }
        if (m_hitReaction) { m_hitReaction->StopAll(); }
    }

    void SceneActor::RequestHitStop(int frames) noexcept
    {
        m_hitStopFrames = std::max(m_hitStopFrames, frames);
    }

    void SceneActor::OverrideHitStop(int frames) noexcept
    {
        NS_ASSERT(Scene, frames >= 0, "OverrideHitStop: 止めの長さ {} が負", frames);
        m_hitStopFrames = std::max(frames, 0);
    }

    void SceneActor::Update()
    {
        // clang-format off
        if (m_effectKeeper) { m_effectKeeper->BeginStep(); }
        if (m_hitReaction) { m_hitReaction->AdvanceOwnerRuns(); }
        // clang-format on
        ObserveStep();
        DecideStep();
        // 止めは状態の時計だけを止める。束で始めた演出は頼まれた係と模型が自分の歩で進める
        if (!IsHitStopping())
        {
            StateStep();
            // 時計は状態の歩なので、状態を進めたフレームにだけ進める
            // clang-format off
            if (m_actionTimeline) { m_actionTimeline->Advance(); }
            // clang-format on
        }
        // StateStep の中で束を始めた時は、そのフレームの身体から止める
        if (!IsHitStopping())
        {
            BodyStep();
        }
        // 置く側の当たりは体を動かした後の姿へ揃える。姿も形も変わっていなければ Collision は何もしない
        // clang-format off
        if (m_collision) { m_collision->SyncToPhysics(); }
        // clang-format on
        // 揺れと震えは体を動かした後の根で書き、VisualStep の残像が書いた後の姿を写し取る
        // clang-format off
        if (m_model) { m_model->AdvanceDraw(); }
        // clang-format on
        VisualStep();
        if (m_hitStopFrames > 0)
        {
            --m_hitStopFrames;
        }
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
