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
#include "NSlib/Core/Windows/Clock.h"

#include <algorithm>
#include <cmath>
#include <optional>

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
        ForEachReflectedClassObj(this, [&](const ClassObjInfo& field, const Object*) {
            if (field.key == key)
            {
                created = true;
            }
        });
        return created;
    }

    void SceneActor::CreateDeclaredSensors()
    {
        SensorDeclarations sensors{this};
        DeclareSensors(sensors);
    }

    void SceneActor::CreateShapeSensor(std::unique_ptr<HitSensor>& member, uint8 kind, HitSensorShape shape)
    {
        ShapeHitSensor* sensor = EmplaceClassObj<HitSensor, ShapeHitSensor>(member, this);
        if (sensor == nullptr)
        {
            return;
        }
        sensor->SetKind(kind);
        sensor->SetShape(shape);
        InitHitSensor(this, sensor);
    }

    void SceneActor::CreateFollowSensor(std::unique_ptr<HitSensor>& member,
                                        uint8 kind,
                                        std::function<SensorVolume(const Matrix& rootWorld)> volume)
    {
        FollowHitSensor* sensor = EmplaceClassObj<HitSensor, FollowHitSensor>(member, this, std::move(volume));
        if (sensor == nullptr)
        {
            return;
        }
        sensor->SetKind(kind);
        InitHitSensor(this, sensor);
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

    Gfx::EffectScene* SceneActor::GetEffectScene() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetEffectScene();
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

    HitSensorDirector* SceneActor::GetHitSensorDirector() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return &m_scene->HitSensors();
    }

    SceneRenderer* SceneActor::GetSceneRenderer() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetSceneRenderer();
    }

    Vector3 SceneActor::GravityDirection() const noexcept
    {
        return Phys::k_GravityDirection;
    }

    void SceneActor::AddGravity(Vector3& velocity, float strength, float dt) const noexcept
    {
        Phys::AddGravity(velocity, strength, dt);
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

    void SceneActor::InitWithCodeValues(std::string_view actorFile)
    {
        m_actorFileName = ClassName();
        if (!actorFile.empty())
        {
            m_actorFileName = std::string{actorFile};
        }
        CreateDeclaredSensors();
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

    void SceneActor::Init(const ActorInitInfo* info)
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
        // センサーは OnInit の前に作るので、OnInit と状態の OnEnter から読める。一覧へ入れるのでシーンを決めた後に作る
        CreateDeclaredSensors();
        OnInit();
        ApplyActorFile(this);
        // 束と動作は .actor だけに書き、シーンでは上書きしない
        // clang-format off
        if (m_hitReaction) { m_hitReaction->ReloadSets(this); }
        if (m_actionTimeline) { m_actionTimeline->ReloadActions(this); }
        // clang-format on
        if (info != nullptr && info->placement != nullptr)
        {
            ApplyActorClassObjs(this, *info->placement);
            ReadSceneOverrides(this, *info->placement);
        }
        // 素材の参照は欄にある。欄を書き終えてから読む
        ReloadAssets();
    }

    void SceneActor::ReloadAssets()
    {
        LoadAssetFields(this);
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
            m_animation->UseModel(m_model.get());
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
        // 描く物は OnInit で、センサーはその前に入れてある。当たりの body は配置と素材を読んだ後の形で 1 回だけ作り、
        // 出ている物だけを OnAppear が物理シーンへ入れる
        // clang-format off
        if (m_collision) { InitCollision(this, m_collision.get(), Root().WorldMatrix()); }
        // clang-format on
        // 一覧には全部のアクターが入っているので、自分より後ろのアクターも取得できる
        ResolveActorRefs();
        OnAppear();
        // 作る時に 1 回だけの物
        // clang-format off
        if (m_animation) { m_animation->BindToModel(m_model.get()); }
        if (m_hitReaction) { m_hitReaction->CreateDirectors(this); }
        // clang-format on
    }

    void SceneActor::OnAppear()
    {
        // 部品をシーンの仕組みへつなぐので、シーンに付いていない間は何もしない
        if (OwningScene() == nullptr || !IsAppeared())
        {
            return;
        }
        // clang-format off
        if (m_model) { ShowModel(this, m_model.get()); }
        if (m_shadow) { ShowModel(this, m_shadow.get()); }
        if (m_collision) { EnableCollision(this, m_collision.get(), Root().WorldMatrix()); }
        if (m_bodySensor) { EnableHitSensor(m_bodySensor.get()); }
        if (m_attackSensor) { EnableHitSensor(m_attackSensor.get()); }
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
        // 描く物・センサー・当たりの body は壊さずに残す。消えている間、描く物は組が無効で、センサーは IsValid が偽になり、
        // 当たりの body は物理シーンの外に出る
        // clang-format off
        if (m_model) { HideModel(this, m_model.get()); }
        if (m_shadow) { HideModel(this, m_shadow.get()); }
        if (m_bodySensor) { DisableHitSensor(m_bodySensor.get()); }
        if (m_attackSensor) { DisableHitSensor(m_attackSensor.get()); }
        if (m_collision) { DisableCollision(this, m_collision.get()); }
        if (m_actionTimeline) { m_actionTimeline->Stop(); }
        if (m_hitReaction) { m_hitReaction->StopAll(this); }
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

    bool SceneActor::StartHitReaction(std::string_view name,
                                      const Vector3& position,
                                      const HitSensor* self,
                                      const HitSensor* other)
    {
        if (!m_hitReaction)
        {
            return false;
        }
        const std::optional<int> stopFrames =
            m_hitReaction->Start(HitReactionSelf{.actor = this, .model = m_model.get()}, name, position, self, other);
        if (!stopFrames.has_value())
        {
            return false;
        }
        RequestHitStop(*stopFrames);
        return true;
    }

    ActionTimelineSelf SceneActor::ActionTimelineSelfOf() noexcept
    {
        return ActionTimelineSelf{.actor = this,
                                  .reaction = m_hitReaction.get(),
                                  .model = m_model.get(),
                                  .bodySensor = m_bodySensor.get(),
                                  .attackSensor = m_attackSensor.get()};
    }

    void SceneActor::Update()
    {
        m_skipStateStep = false;
        // clang-format off
        if (m_effectKeeper) { m_effectKeeper->BeginStep(GetEffectScene()); }
        if (m_hitReaction) { m_hitReaction->AdvanceOwnerRuns(); }
        // clang-format on
        DecideStep();
        // 止めは状態の時計だけを止める。束で始めた演出は頼まれた係と模型が自分の歩で進める
        if (!IsHitStopping() && !m_skipStateStep)
        {
            // 時計は状態の歩なので、状態を進めたフレームにだけ進める
            // clang-format off
            if (m_stateMachine) { m_stateMachine->Update(GetFrameDelta(this)); }
            if (m_actionTimeline) { m_actionTimeline->Advance(ActionTimelineSelfOf()); }
            // clang-format on
            AfterStateStep();
        }
        // 状態機械の中で束を始めた時は、そのフレームの身体から止める
        if (!IsHitStopping())
        {
            BodyStep();
        }
        // 置く側の当たりは体を動かした後の姿へ揃える。姿も形も変わっていなければ Collision は何もしない
        // 状態機械の中で消えたアクターの body は物理シーンの外にあるので揃えない
        // clang-format off
        if (m_collision && IsAppeared()) { m_collision->SyncToPhysics(Root().WorldMatrix(), this); }
        // clang-format on
        // 揺れと震えは体を動かした後の根で書き、VisualStep の残像が書いた後の姿を写し取る
        // clang-format off
        if (m_model) { m_model->AdvanceDraw(this, Root().Position()); }
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

    void SceneActor::PrepareRender()
    {
        // clang-format off
        if (m_animation) { m_animation->Update(m_model.get(), GetFrameDelta(this)); }
        // clang-format on
    }

    void SceneActor::OnEndPlay()
    {
        // 描く物・センサー・当たりの body は消えても残っているので、出ていたかを問わずここで外す
        Disappear();
        if (OwningScene() == nullptr)
        {
            return;
        }
        // clang-format off
        if (m_model) { EndDraw(this, m_model.get()); }
        if (m_shadow) { EndDraw(this, m_shadow.get()); }
        if (m_bodySensor) { EndHitSensor(this, m_bodySensor.get()); }
        if (m_attackSensor) { EndHitSensor(this, m_attackSensor.get()); }
        if (m_collision) { EndCollision(this, m_collision.get()); }
        // clang-format on
    }

} // namespace NS
