#include "NSlib/CoreObject/SceneActor.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/CoreObject/Action/ActorAction.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/Asset/AnimClip.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Effect.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/ClassObjs/Collision.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/CoreObject/ClassObjs/Sound.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/ActorBuilder.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Phys/PhysicsScene.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <variant>

namespace NS
{

    NS_REFLECT_BEGIN_IMPL(SceneActor, Object)
    NS_CLASSOBJ(m_transform, "Transform")
    NS_CLASSOBJ(m_model, "Model")
    NS_CLASSOBJ(m_animation, "Animation")
    NS_CLASSOBJ(m_shadow, "Shadow")
    NS_CLASSOBJ(m_collision, "Collision")
    NS_CLASSOBJ(m_hitSensors, "HitSensors")
    NS_CLASSOBJ(m_hitReaction, "HitReaction")
    NS_CLASSOBJ(m_effect, "Effect")
    NS_CLASSOBJ(m_sound, "Sound")
    // 保存のキーは改名の前の名前のまま。変えると、今ある .actor とシーンのファイルの部品の行が読めなくなる
    NS_CLASSOBJ(m_actorAction, "ActionTimeline")
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
        if (m_parent != nullptr && !m_parent->IsAppeared())
        {
            return;
        }
        if (m_presence != Presence::Disappeared)
        {
            return;
        }
        m_presence = Presence::Appeared;
        OnAppear();
        for (const std::unique_ptr<SceneActor>& child : m_children)
        {
            child->Appear();
        }
    }

    void SceneActor::Disappear() noexcept
    {
        if (m_presence != Presence::Appeared)
        {
            return;
        }
        m_presence = Presence::Disappeared;
        OnDisappear();
        for (const std::unique_ptr<SceneActor>& child : m_children)
        {
            child->Disappear();
        }
    }

    void SceneActor::Destroy() noexcept
    {
        if (m_parent != nullptr)
        {
            NS_LOG_ERROR(
                Scene, "子の {} は親と一緒に終わるので、単独では消さない。外すなら Disappear を使う", ClassName());
            return;
        }
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

    Audio::AudioDevice* SceneActor::GetAudioDevice() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetAudioDevice();
    }

    CameraManager* SceneActor::GetCameraManager() const noexcept
    {
        if (m_scene == nullptr)
        {
            return nullptr;
        }
        return m_scene->GetCameraManager();
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
        if (!std::isfinite(rate) || !IsPositive(rate))
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

    void SceneActor::InitChild(std::unique_ptr<SceneActor> child)
    {
        SceneActor* raw = child.get();
        m_children.push_back(std::move(child));
        raw->m_parent = this;
        raw->m_scene = m_scene;
        raw->InitWithCodeValues({});
        // 子はコンストラクタで出ているので、隠れている親の子は隠す
        if (!IsAppeared())
        {
            raw->Disappear();
        }
    }

    void SceneActor::InitWithCodeValues(std::string_view actorFile)
    {
        m_actorFileName = ClassName();
        if (!actorFile.empty())
        {
            m_actorFileName = std::string{actorFile};
        }
        OnInit();
    }

    bool SceneActor::IsSceneOverride(std::string_view part, std::string_view field) const
    {
        return m_sceneOverrides.contains(SceneOverrideKey{std::string{part}, std::string{field}});
    }

    void SceneActor::AddSceneOverride(std::string_view part, std::string_view field)
    {
        m_sceneOverrides.insert(SceneOverrideKey{std::string{part}, std::string{field}});
    }

    void SceneActor::RemoveSceneOverride(std::string_view part, std::string_view field)
    {
        m_sceneOverrides.erase(SceneOverrideKey{std::string{part}, std::string{field}});
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
        OnInit();
        ApplyActorFile(this);
        ReloadActorFileOnlyValues();
        if (info != nullptr && info->placement != nullptr)
        {
            ApplyActorClassObjs(this, *info->placement);
            ReadSceneOverrides(this, *info->placement);
        }
        // 素材の参照は欄にある。欄を書き終えてから読む
        ReloadAssets();
    }

    void SceneActor::ReloadActorFileOnlyValues()
    {
        ReloadActorFileOnlyValuesExceptActions();
        ReloadActions();
    }

    void SceneActor::ReloadActions()
    {
        if (!m_actorAction)
        {
            return;
        }
        m_actorAction->ReloadActions(ActorFileName());
        RunActionOutputs();
        PreloadActionSounds();
    }

    void SceneActor::ReloadActorFileOnlyValuesExceptActions()
    {
        // clang-format off
        if (m_hitSensors) { m_hitSensors->ReloadValues(this); }
        if (m_hitReaction) { m_hitReaction->ReloadSets(this); }
        // clang-format on
        PreloadHitReactionSounds();
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
        for (const std::unique_ptr<SceneActor>& child : m_children)
        {
            child->ReloadAssets();
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

    void SceneActor::Start()
    {
        if (m_started)
        {
            return;
        }
        m_started = true;
        OnStart();
        for (const std::unique_ptr<SceneActor>& child : m_children)
        {
            child->Start();
        }
    }

    void SceneActor::OnStart()
    {
        // 描く物とセンサーは OnInit で入れてある。当たりの body は配置と素材を読んだ後の形で 1 回だけ作り、
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
        if (m_hitSensors) { m_hitSensors->EnableAll(); }
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
        // 描く物・センサー・当たりの body は壊さずに残す。消えている間、描く物は組が無効で、センサーは IsValid
        // が偽になり、 当たりの body は物理シーンの外に出る
        // clang-format off
        if (m_model) { HideModel(this, m_model.get()); }
        if (m_shadow) { HideModel(this, m_shadow.get()); }
        // 動作は外す前に止める。入り切りの途中のセンサーを戻す頼みが、外したセンサーを開け直さないため
        if (m_actorAction) { StopAction(); }
        if (m_hitSensors) { m_hitSensors->DisableAll(); }
        if (m_collision) { DisableCollision(this, m_collision.get()); }
        if (m_hitReaction) { m_hitReaction->StopAll(HitReactionSelf{.actor = this, .model = m_model.get()}); }
    }

    namespace
    {
        // action のフレーム 0 に、clip を流すアニメーションの項目がある場合 true。clip が nullptr なら false
        bool PlaysClipAtFirstFrame(const ActorActionInfo& action, const Anim::AnimClip* clip) noexcept
        {
            if (clip == nullptr)
            {
                return false;
            }
            for (const ActorActionEvent& event : action.events)
            {
                const ActionAnimInfo* anim = std::get_if<ActionAnimInfo>(&event.item);
                if (event.start == 0 && anim != nullptr && anim->clip != nullptr && anim->clip->Clip() == clip)
                {
                    return true;
                }
            }
            return false;
        }

        // 動作の項目 1 つを、持ち主の部品と場面の管理クラスへ頼む
        struct ActionItemRunner
        {
            SceneActor& actor;
            int length = 0; // 項目の長さ (フレーム数)

            void operator()(const ActionAnimInfo& anim) const
            {
                Animation* animation = actor.AnimationClassObj();
                if (animation == nullptr || anim.clip == nullptr || anim.clip->Clip() == nullptr)
                {
                    return;
                }
                animation->SetLooping(anim.looping);
                animation->Play(anim.clip->Clip(), anim.blendSeconds);
            }

            void operator()(const ActionSoundInfo& sound) const { PlaySound(&actor, sound.sound, sound.volume); }

            void operator()(const ActionEffectInfo& effect) const
            {
                EffectObj* effects = actor.GetEffect();
                if (effects == nullptr || effect.effect == nullptr)
                {
                    return;
                }
                Gfx::EffectPlayDesc desc;
                desc.position = actor.Root().Position();
                (void)effects->Play(actor.GetEffectScene(), effect.effect, desc);
            }

            void operator()(const ActionPadInfo& pad) const
            {
                Scene* scene = actor.OwningScene();
                if (scene == nullptr)
                {
                    return;
                }
                const PadRumbleDesc vibration{.left = pad.left, .right = pad.right, .frames = length};
                if (pad.layered)
                {
                    scene->PadRumble().Blend(&actor, vibration);
                    return;
                }
                scene->PadRumble().Start(&actor, vibration);
            }

            void operator()(const ActionCameraInfo& camera) const
            {
                // カメラの無い場面 (試しの台) では揺らす先が無い
                if (actor.GetCameraManager() == nullptr)
                {
                    return;
                }
                CameraTraumaDesc desc;
                desc.trauma = camera.trauma;
                if (!AddCameraTrauma(&actor, desc))
                {
                    NS_LOG_WARN(Scene, "動作のカメラの揺れのトラウマ {} が壊れていて、揺らさなかった", camera.trauma);
                }
            }

            void operator()(const ActionScreenInfo& screen) const
            {
                Scene* scene = actor.OwningScene();
                if (scene == nullptr)
                {
                    return;
                }
                scene->HitScreen().StartFlash(&actor, length, screen.alpha);
            }
        };
    } // namespace

    void SceneActor::RequestHitStop(int frames) noexcept { m_hitStopFrames = std::max(m_hitStopFrames, frames); }

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

    std::vector<HitOwnerRun> SceneActor::StartHitReactionRuns(std::string_view name,
                                                              const Vector3& position,
                                                              const HitSensor* self,
                                                              const HitSensor* other)
    {
        if (!m_hitReaction)
        {
            return {};
        }
        const std::size_t first = m_hitReaction->OwnerRuns().size();
        (void)StartHitReaction(name, position, self, other);
        return m_hitReaction->OwnerRunsSince(first);
    }

    bool SceneActor::StartAction(std::string_view name)
    {
        if (!m_actorAction)
        {
            NS_LOG_WARN(Scene, "{} は ActorAction を持たないので、動作 {} を始めなかった", ClassName(), name);
            return false;
        }
        if (!m_actorAction->Start(name))
        {
            NS_LOG_WARN(Scene, ".actor {} に動作 {} が無いので、始めなかった", ActorFileName(), name);
            return false;
        }
        // 次の Update まで待つと、フレーム 0 のクリップの切り替えが 1 フレーム遅れる
        AdvanceAction();
        // フレーム 0 の項目がクリップを替えるので、当てた後のクリップに結ぶ。自分のフレーム 0 にそのクリップの
        // 項目が無い動作は、ほかの動作が流したクリップに結ばず、フレーム 0 のままにする
        ActionClock clock = CurrentActionClock();
        if (!PlaysClipAtFirstFrame(*m_actorAction->CurrentAction(), clock.clip))
        {
            clock.clip = nullptr;
        }
        m_actorAction->BindClip(clock);
        return true;
    }

    bool SceneActor::StartStateAction(std::string_view name)
    {
        const IStateMachine* machine = GetStateMachine();
        if (!m_actorAction || machine == nullptr)
        {
            NS_LOG_WARN(
                Scene, "{} は ActorAction か状態機械を持たないので、動作 {} を始めなかった", ClassName(), name);
            return false;
        }
        if (!m_actorAction->StartWithState(name, *machine))
        {
            NS_LOG_WARN(Scene, ".actor {} に動作 {} が無いので、始めなかった", ActorFileName(), name);
            return false;
        }
        return true;
    }

    void SceneActor::StopAction()
    {
        if (!m_actorAction)
        {
            return;
        }
        m_actorAction->Stop();
        RunActionOutputs();
    }

    void SceneActor::AdvanceAction()
    {
        m_actorAction->Advance(CurrentActionClock());
        RunActionOutputs();
    }

    ActionClock SceneActor::CurrentActionClock() const noexcept
    {
        ActionClock clock{.machine = GetStateMachine()};
        if (m_animation)
        {
            clock.clip = m_animation->CurrentClip();
            clock.clipTime = m_animation->Time();
            clock.clipLoops = m_animation->LoopCount();
        }
        return clock;
    }

    void SceneActor::PreloadActionSounds()
    {
        if (!m_sound)
        {
            return;
        }
        for (const ActorActionInfo& action : m_actorAction->Actions())
        {
            for (const ActorActionEvent& event : action.events)
            {
                if (const ActionSoundInfo* sound = std::get_if<ActionSoundInfo>(&event.item))
                {
                    m_sound->Preload(&AssetManager::Get(), sound->sound.name);
                }
            }
        }
    }

    void SceneActor::PreloadHitReactionSounds()
    {
        if (!m_sound || !m_hitReaction)
        {
            return;
        }
        for (const HitReactionSet& set : m_hitReaction->Sets())
        {
            for (const HitReactionEntry& entry : set.entries)
            {
                if (const HitSoundInfo* sound = entry.item.As<HitSoundInfo>())
                {
                    m_sound->Preload(&AssetManager::Get(), sound->sound.name);
                }
            }
        }
    }

    void SceneActor::RunActionOutputs()
    {
        // 入り切りは名前でセンサーを引く。持たないセンサーは .actor の誤りなので知らせて飛ばす
        for (const ActionSensorChange& change : m_actorAction->TakeSensorChanges())
        {
            HitSensor* sensor = nullptr;
            if (m_hitSensors)
            {
                sensor = m_hitSensors->Find(change.sensor);
            }
            if (sensor == nullptr)
            {
                NS_LOG_WARN(Scene,
                            ".actor {} の動作は、{} が持たないセンサー {} を指すので、切り替えなかった",
                            ActorFileName(),
                            ClassName(),
                            change.sensor);
                continue;
            }
            if (change.valid)
            {
                sensor->Validate();
            }
            else
            {
                sensor->Invalidate();
            }
        }
        const ActorActionInfo* action = m_actorAction->CurrentAction();
        if (action == nullptr)
        {
            return;
        }
        for (const std::size_t index : m_actorAction->StartedEvents())
        {
            const ActorActionEvent& event = action->events[index];
            std::visit(ActionItemRunner{.actor = *this, .length = event.length}, event.item);
        }
    }

    bool SceneActor::IsStopped() const noexcept
    {
        if (m_hitStopFrames > 0)
        {
            return true;
        }
        return m_scene != nullptr && m_scene->IsSceneStopped() && !m_scene->IsStopSceneMover(this);
    }

    void SceneActor::Update()
    {
        m_skipStateStep = false;
        // clang-format off
        if (m_effect) { m_effect->BeginStep(GetEffectScene()); }
        if (m_hitReaction) { m_hitReaction->AdvanceOwnerRuns(); }
        // clang-format on
        DecideStep();
        // 止めは状態の時計だけを止める。束で始めた演出は頼まれた係と模型が自分の歩で進める
        if (!IsStopped() && !m_skipStateStep)
        {
            // 時計は状態の歩なので、状態を進めたフレームにだけ進める
            // clang-format off
            if (m_stateMachine) { m_stateMachine->Update(GetFrameDelta(this)); }
            if (m_actorAction) { AdvanceAction(); }
            // clang-format on
            AfterStateStep();
        }
        // 状態機械の中で束を始めた時は、そのフレームの身体から止める
        if (!IsStopped())
        {
            BodyStep();
        }
        // 置く側の当たりは体を動かした後の姿へ揃える。姿も形も変わっていなければ Collision は何もしない
        // 状態機械の中で消えたアクターの body は物理シーンの外にあるので揃えない
        // clang-format off
        if (m_collision && IsAppeared()) { m_collision->SyncToPhysics(Root().WorldMatrix(), this); }
        // clang-format on
        // clang-format off
        if (m_hitReaction) { m_hitReaction->AdvanceRuns(HitReactionSelf{.actor = this, .model = m_model.get()}); }
        // clang-format on
        for (const std::unique_ptr<SceneActor>& child : m_children)
        {
            if (child->IsAppeared())
            {
                child->Update();
            }
        }
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
        for (const std::unique_ptr<SceneActor>& child : m_children)
        {
            child->SnapshotForInterpolation();
        }
    }

    void SceneActor::PrepareRender()
    {
        // clang-format off
        if (m_animation) { m_animation->Update(m_model.get(), GetFrameDelta(this)); }
        // clang-format on
        for (const std::unique_ptr<SceneActor>& child : m_children)
        {
            if (child->IsAppeared())
            {
                child->PrepareRender();
            }
        }
    }

    void SceneActor::OnEndPlay()
    {
        for (const std::unique_ptr<SceneActor>& child : m_children)
        {
            child->OnEndPlay();
        }
        // 描く物・センサー・当たりの body は消えても残っているので、出ていたかを問わずここで外す
        Disappear();
        if (OwningScene() == nullptr)
        {
            return;
        }
        // clang-format off
        if (m_model) { EndDraw(this, m_model.get()); }
        if (m_shadow) { EndDraw(this, m_shadow.get()); }
        if (m_hitSensors) { m_hitSensors->EndAll(this); }
        if (m_collision) { EndCollision(this, m_collision.get()); }
        // clang-format on
    }

} // namespace NS
