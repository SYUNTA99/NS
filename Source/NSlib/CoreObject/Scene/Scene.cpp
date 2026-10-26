#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/Action/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ActorBuilder.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/UIActor.h"
#include "NSlib/Gfx/Render/DebugDraw.h"
#include "NSlib/Gfx/Render/EffectScene.h"
#include "NSlib/Core/Windows/Clock.h"

#include <magic_enum/magic_enum.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

namespace
{
    // 世界の速さの溜めが 1 に届いたと見なす余白
    constexpr float k_WorldCarryTolerance = 1.0e-4f;
} // namespace

namespace NS
{
    Scene::Scene()
    {
        m_mainCamera.SetUp({0.0f, 1.0f, 0.0f});
        m_cameraManager.SetCamera(&m_mainCamera);
        // Jolt の broadphase の木は、アクターが全部動いた後で組み直す
        AddSystemTick(m_physics, UpdatePhase::Physics);
        // 当たりの調べ役はセンサーの段 (物理の後、コースの前) で回る
        AddSystemTick(m_hitSensors, UpdatePhase::Sensors);
        // 仮想カメラはアクターとコースが動かした後の相手を追い、続けて選び直す
        AddSystemTick(m_cameraManager, UpdatePhase::Camera);
        m_cameraManager.SetCollision(this);
        // エフェクトの世界は同じ歩の SceneActor が出した演出を受けてから進む
        AddSystemTick(m_sceneRenderer, UpdatePhase::Effects);
        AddSystemTick(m_ui, UpdatePhase::UI);
        // 暗転は UI の後に進める。UI の段は実時間なので、ヒットストップと遅い世界でも同じ速さで進む
        AddSystemTick(m_fade, UpdatePhase::UI);
    }

    Scene::~Scene() = default;

    void Scene::ResetFromJson(nlohmann::json scene)
    {
        SetEnvironment(ReadSceneEnvironment(scene));
        // 組む前に番号を揃える。未採番のまま組むと id で名指しできない実体ができる
        EnsureUniqueActorIds(scene);
        RebuildSceneFrom(scene);
    }

    nlohmann::json Scene::ToJson() const
    {
        nlohmann::json scene = MakeSceneJson();
        WriteSceneEnvironment(scene, m_environment);
        SetSceneJsonNextActorId(scene, m_actors.NextActorId());
        nlohmann::json& objects = SceneJsonActors(scene);
        for (const SceneActor* obj : m_actors)
        {
            objects.push_back(ActorToJson(obj));
        }
        for (const nlohmann::json& object : m_unresolvedActors)
        {
            objects.push_back(object);
        }
        return scene;
    }

    void Scene::SetEnvironment(const SceneEnvironment& environment) noexcept
    {
        m_environment = environment;
    }

    void Scene::SetSimulationEnabled(bool enabled) noexcept
    {
        m_simulationEnabled = enabled;
        m_simulationPaused = false;
        m_simulationStepFrames = 0;
        // 遅い世界はプレイの中の状態なので、入れ直したプレイへ持ち越さない
        m_worldSpeed = 1.0f;
        m_worldCarry = 0.0f;
        m_rampActive = false;
        m_othersHeldSteps = 0;
    }

    void Scene::StepSimulation() noexcept
    {
        m_simulationPaused = true;
        m_simulationStepFrames += 1;
    }

    void Scene::SetWorldSpeed(float speed) noexcept
    {
        // 非数は Clamp を素通りして溜めを壊すので受けない
        if (!std::isfinite(speed))
        {
            return;
        }
        m_worldSpeed = Clamp(speed, 0.0f, 1.0f);
        m_rampActive = false;
    }

    void Scene::HoldOthers(const SceneActor* holder, int steps) noexcept
    {
        NS_ASSERT(Scene, holder != nullptr, "Scene::HoldOthers: 止め始めるアクターが無い");
        // 止めている間は先に止め始めたアクターが持つ。後から頼んだアクターに替えると、止め始めたアクターが止まる
        if (m_othersHeldSteps > 0 && !IsOthersHolder(holder))
        {
            return;
        }
        if (steps <= 0)
        {
            m_othersHeldSteps = 0;
            return;
        }
        m_othersHeldSteps = std::max(m_othersHeldSteps, steps);
        m_othersHolder = holder;
        m_othersHolderId = holder->Id();
    }

    bool Scene::IsOthersHolder(const SceneActor* actor) const noexcept
    {
        // id の無い JSON から読んだアクターは id が 0 で重なるので、番地も比べる。番地は比べるだけで読まない
        return actor == m_othersHolder && actor->Id() == m_othersHolderId;
    }

    bool Scene::UpdatesWhileHeld(const SceneActor* actor) const noexcept
    {
        return m_othersHeldSteps <= 0 || IsOthersHolder(actor);
    }

    void Scene::StartWorldSpeedRamp(float fromSpeed, float realSeconds, const Curve& shape) noexcept
    {
        if (!std::isfinite(fromSpeed) || !std::isfinite(realSeconds))
        {
            return;
        }
        m_rampActive = false;
        if (realSeconds <= 0.0f)
        {
            m_worldSpeed = 1.0f;
            return;
        }
        m_worldSpeed = Clamp(fromSpeed, 0.0f, 1.0f);
        m_rampActive = true;
        m_rampFrom = m_worldSpeed;
        m_rampSeconds = realSeconds;
        m_rampElapsed = 0.0f;
        m_rampShape = shape;
    }

    void Scene::AdvanceWorldSpeedRamp() noexcept
    {
        if (!m_rampActive)
        {
            return;
        }
        // 世界の時間で数えると、遅い分だけ戻るのが延びる
        m_rampElapsed += OS::FrameTimer::FixedDelta();
        const float ratio = m_rampElapsed / m_rampSeconds;
        if (ratio >= 1.0f - k_WorldCarryTolerance)
        {
            m_worldSpeed = 1.0f;
            m_rampActive = false;
            return;
        }
        float recovered = ratio;
        if (m_rampShape.count > 0)
        {
            recovered = Clamp(m_rampShape.Evaluate(ratio), 0.0f, 1.0f);
        }
        m_worldSpeed = m_rampFrom + (1.0f - m_rampFrom) * recovered;
    }

    float Scene::RenderAlpha(float frameAlpha) const noexcept
    {
        if (!m_simulationEnabled || m_simulationPaused)
        {
            return 1.0f;
        }
        if (m_worldSpeed >= 1.0f)
        {
            return frameAlpha;
        }
        return std::min(m_worldCarry + frameAlpha * m_worldSpeed, 1.0f);
    }

    bool Scene::AdvanceWorldClock() noexcept
    {
        m_worldCarry += m_worldSpeed;
        // 0.2 を 5 回足しても 1 にわずかに届かない事がある
        if (m_worldCarry < 1.0f - k_WorldCarryTolerance)
        {
            return false;
        }
        m_worldCarry = std::max(m_worldCarry - 1.0f, 0.0f);
        return true;
    }

    SceneActor* Scene::AdoptActor(std::unique_ptr<SceneActor> obj, std::string name)
    {
        if (!obj)
        {
            return nullptr;
        }

        // シーンは SpawnActor の作る関数が OnInit の前に決めている
        SceneActor* raw = m_actors.AppendWithNewId(std::move(obj), std::move(name));
        if (raw == nullptr)
        {
            return nullptr;
        }
        StartSpawned(raw);
        return raw;
    }

    void Scene::StartSpawned(SceneActor* obj)
    {
        NS_ASSERT(Scene, obj != nullptr, "Scene::StartSpawned: 開始するアクターが無い");
        // 素材は作る関数の Init が読み終えている
        obj->OnStart();
        // 後から入る物は他のアクターが既に揃っている
        obj->InitAfterPlacement();
    }

    std::unique_ptr<SceneActor> Scene::BuildOrKeepUnresolved(const nlohmann::json& actorJson)
    {
        std::unique_ptr<SceneActor> built = ActorFromJson(actorJson, *this);
        const uint32 id = ActorJsonId(actorJson);
        std::vector<nlohmann::json>::iterator kept = std::find_if(
            m_unresolvedActors.begin(), m_unresolvedActors.end(), [id](const nlohmann::json& unresolved) {
                return ActorJsonId(unresolved) == id;
            });
        if (built)
        {
            if (kept != m_unresolvedActors.end())
            {
                m_unresolvedActors.erase(kept);
            }
            return built;
        }
        // 同じ id の物は 1 つだけ持つ。戻しの控えを何度当てても保存のアクターは増えない
        if (kept != m_unresolvedActors.end())
        {
            *kept = actorJson;
        }
        else
        {
            m_unresolvedActors.push_back(actorJson);
        }
        return nullptr;
    }

    SceneActor* Scene::SpawnFromJson(const nlohmann::json& actorJson)
    {
        std::unique_ptr<SceneActor> built = BuildOrKeepUnresolved(actorJson);
        if (!built)
        {
            return nullptr;
        }
        SceneActor* raw = m_actors.InsertFromJson(std::move(built), actorJson, m_actors.ActorCount());
        if (raw == nullptr)
        {
            return nullptr;
        }
        StartSpawned(raw);
        return raw;
    }

    SceneActor* Scene::ApplyFromJson(const nlohmann::json& snapshot, std::unique_ptr<SceneActor>* replaced)
    {
        NS_ASSERT(Scene, replaced != nullptr, "作り直した時の古いアクターを受ける先が無い");
        SceneActor* obj = m_actors.FindByActorId(ActorJsonId(snapshot));
        if (obj == nullptr)
        {
            return SpawnFromJson(snapshot);
        }
        // 部品はクラスで決まるので、同じクラスなら .actor が替わっても作り直さずに値を書き直せる
        const std::string_view actorFile = ActorJsonFile(snapshot);
        const std::string className = ActorFileLibrary::Get().ClassNameOf(actorFile);
        if (!className.empty() && className != obj->ClassName())
        {
            NS_LOG_ERROR(Scene,
                         "id {} のアクターの控えの .actor {} はクラス {} で、今のクラス {} と違うので当てない",
                         obj->Id(),
                         actorFile,
                         className,
                         obj->ClassName());
            return nullptr;
        }
        // 控えはシーンの上書きだけを持つ。上書きの無い項目も .actor の値へ戻すため、全部の値に重ねてから写す
        const nlohmann::json* values = ActorFileLibrary::Get().Values(actorFile);
        if (values == nullptr)
        {
            return RebuildActorFromJson(snapshot, replaced);
        }
        const std::optional<nlohmann::json> merged = MergeLayer(*values, snapshot, FieldDestination::Scene, obj);
        if (!merged.has_value())
        {
            return RebuildActorFromJson(snapshot, replaced);
        }
        const nlohmann::json& object = *merged;
        const bool actorFileChanged = actorFile != obj->ActorFileName();
        m_actors.ApplyIdentity(obj, ReadActorHeader(snapshot));
        ReadSceneOverrides(obj, snapshot);
        bool written = false;
        ForEachReflectedClassObj(obj, [&](const ClassObjInfo& field, Object* classObj) {
            const nlohmann::json* fields = ClassObjFields(object, field.key);
            if (fields == nullptr || SerializeClassObjFields(classObj) == *fields)
            {
                return;
            }
            (void)ApplyJsonFields(classObj, *fields);
            written = true;
        });
        // 束と動作は .actor だけに書くので、替えた先の .actor から読む
        if (actorFileChanged)
        {
            if (HitReaction* reaction = obj->HitReactionClassObj())
            {
                reaction->ReloadSets(obj);
            }
            if (ActionTimeline* timeline = obj->ActionTimelineClassObj())
            {
                timeline->ReloadActions(obj);
            }
        }
        // 書き換えた欄に参照があれば読み直す。作る時と同じ組を通る
        if (written || actorFileChanged)
        {
            obj->ReloadAssets();
        }
        // 書き換えた欄に ActorRef があれば、指す相手を取得し直す
        if (written)
        {
            obj->ResolveActorRefs();
        }
        return obj;
    }

    SceneActor* Scene::RebuildActorFromJson(const nlohmann::json& snapshot, std::unique_ptr<SceneActor>* replaced)
    {
        // 古いアクターは解放せずに呼び手へ渡す。外したアクターの番地に新しいアクターが作られない
        DetachedActor detached = m_actors.DetachByActorId(ActorJsonId(snapshot));
        if (!detached.actor)
        {
            return nullptr;
        }
        *replaced = std::move(detached.actor);
        // 組めない姿は居ない姿として扱い、JSON は BuildOrKeepUnresolved が持つ
        std::unique_ptr<SceneActor> built = BuildOrKeepUnresolved(snapshot);
        if (!built)
        {
            return nullptr;
        }
        SceneActor* raw = m_actors.InsertFromJson(std::move(built), snapshot, detached.index);
        if (raw == nullptr)
        {
            return nullptr;
        }
        StartSpawned(raw);
        return raw;
    }

    void Scene::RemoveActor(uint32 objectId)
    {
        m_actors.RemoveByActorId(objectId);
        std::erase_if(m_unresolvedActors, [objectId](const nlohmann::json& unresolved) {
            return ActorJsonId(unresolved) == objectId;
        });
    }

    DetachedActor Scene::DetachActor(uint32 objectId)
    {
        DetachedActor detached = m_actors.DetachByActorId(objectId);
        std::erase_if(m_unresolvedActors, [objectId](const nlohmann::json& unresolved) {
            return ActorJsonId(unresolved) == objectId;
        });
        return detached;
    }

    SceneActor* Scene::ReattachActor(DetachedActor detached)
    {
        NS_ASSERT(Scene,
                  !detached.actor || detached.actor->OwningScene() == this,
                  "別のシーンから外したアクターを戻そうとしている");
        return m_actors.Reattach(std::move(detached));
    }

    void Scene::ClearEffects() noexcept
    {
        Gfx::EffectScene* effects = m_sceneRenderer.Effects();
        if (effects != nullptr)
        {
            effects->StopAll();
            effects->Update(0.0f);
        }
    }

    void Scene::ClearWorld()
    {
        // シーンに 1 つの物 (進行役など) はアクターと画面の一覧を借りるので先に捨てる。
        // 組み直しでは、組み直したアクターの開始が要る物が最初の状態から作り直す
        m_sceneObjs.Clear();
        Fade().Cancel();
        m_actors.Clear();
        m_unresolvedActors.clear();
        ClearEffects();
#if !defined(NS_SHIPPING)
        // 図形は前の世界の姿で、溜め場は静的な 1 つ。残すと、次の歩が来るまで消えた物の線が出る
        Gfx::DebugDraw::Clear();
#endif
    }

    void Scene::RebuildSceneFrom(const nlohmann::json& scene)
    {
        ClearWorld();
        // 仮想カメラの付け外しは、持ち主の SceneActor が OnInit / OnEndPlay で行う
        // .actor の見つからないアクターは作らず、保存で消さないように JSON のまま持つ
        m_actors.RebuildFromJson(
            scene, *this, [this](const nlohmann::json& entry) { return BuildOrKeepUnresolved(entry); });
        // 当たりはアクターの開始で入っている。まとめて入れた後の broadphase だけを張り直す
        m_physics.Physics().OptimizeBroadPhase();
    }

    void Scene::OnUpdate()
    {
        // 世界の駆動。読み込んだら回り続けるのが既定で、編集モードのエディタだけが止める
        // 時間停止中は前の値の控えだけが残り、previous == current で補間が凍る
        // Actor の Update が回らない歩も、編集で動いた当たりを揃える
        if (!m_simulationEnabled)
        {
            m_actors.SnapshotActors();
            m_actors.SyncCollisions();
            return;
        }

        if (m_simulationPaused)
        {
            if (m_simulationStepFrames <= 0)
            {
                m_actors.SnapshotActors();
                m_actors.SyncCollisions();
                return;
            }
            // コマ送りは実時間の 1 歩。遅い世界では世界が進まない歩もある
            m_simulationStepFrames -= 1;
        }

        const bool worldStep = AdvanceWorldClock();
        if (worldStep)
        {
            // 補間描画用。全アクターの根の Transform と Model の前の値を控える
            // 世界を進めない歩に控えると previous == current になり、遅い世界の補間が止まる
            m_actors.SnapshotActors();
#if !defined(NS_SHIPPING)
            // 描画 1 回ごとに捨てると、その間に進む固定ステップの回数で映る図形が変わる
            Gfx::DebugDraw::BeginStep();
#endif
        }
        for (UpdatePhase phase : magic_enum::enum_values<UpdatePhase>())
        {
            if (!worldStep && ClockOf(phase) == PhaseClock::World)
            {
                continue;
            }
            // 止めはアクターの Update の中で置かれるので、毎段読み直してその歩の残りの段から効かせる
            if (m_othersHeldSteps > 0 && !RunsWhileOthersHeld(phase))
            {
                continue;
            }
            ExecutePhase(phase);
        }
        m_actors.RemoveDestroyed();
        // 止めは世界を進めた歩で数える。遅い世界でも、止めた歩の数だけ止め始めたアクターが進む
        if (worldStep && m_othersHeldSteps > 0)
        {
            m_othersHeldSteps -= 1;
        }
        // この歩は始めた時の速さで回し、次の歩の速さを書く
        AdvanceWorldSpeedRamp();
    }

    void Scene::OnShutdown()
    {
        ClearWorld();
        m_cameraManager.SetCamera(nullptr);
    }

    void Scene::OnRender()
    {
        // 更新は終わっているので bounds は 1 フレームに 1 回で足りる。ビューを何枚描いても同じ値
        m_sceneRenderer.SyncRenderBounds();

        // 世界が実時間で進まない間は、前の固定フレームからの経過の割合に意味が無い
        // 描く度に割合が変わると、同じフレームの絵が揺れる
        const float alpha = RenderAlpha(OS::FrameTimer::Alpha());
        // 姿勢はビューに依らないので 1 フレームに 1 回。止めている間の実カメラは止めた側が書く
        if (m_simulationEnabled)
        {
            m_cameraManager.Evaluate(alpha);
        }
        m_sceneRenderer.Render(m_mainCamera, m_ui, m_fade.Fade(), m_environment.skybox, alpha);
    }

    SceneRenderer* Scene::GetSceneRenderer() const noexcept
    {
        return const_cast<SceneRenderer*>(&m_sceneRenderer);
    }

    CameraManager* Scene::GetCameraManager() const noexcept
    {
        return const_cast<CameraManager*>(&m_cameraManager);
    }

    SceneSystemHolder* Scene::GetSceneSystemHolder() const noexcept
    {
        return &m_sceneObjs;
    }

    Phys::PhysicsScene* Scene::GetPhysicsScene() const noexcept
    {
        // 地形を問う関数のほか、Collision も持ち主の Scene の body を出し入れする時にここから引く
        return const_cast<Phys::PhysicsScene*>(&m_physics.Physics());
    }

    Gfx::EffectScene* Scene::GetEffectScene() const noexcept
    {
        return m_sceneRenderer.Effects();
    }

    void Scene::AddSystemTick(SceneSystem& system, UpdatePhase phase)
    {
        for (SystemEntry& entry : m_systems)
        {
            if (entry.system == &system)
            {
                entry.phase = phase;
                return;
            }
        }
        m_systems.push_back(SystemEntry{.system = &system, .phase = phase});
    }

    void Scene::RemoveSystemTick(SceneSystem& system) noexcept
    {
        std::erase_if(m_systems, [&system](const SystemEntry& entry) noexcept { return entry.system == &system; });
    }

    void Scene::ExecutePhase(UpdatePhase phase)
    {
        m_actors.BeginPhase();
        // 段で呼ぶ管理役は SceneActor を回す前に決める。段の SceneActor が足した管理役はこの段では呼ばない
        m_scheduledSystems.clear();
        for (const SystemEntry& entry : m_systems)
        {
            if (entry.phase == phase)
            {
                m_scheduledSystems.push_back(entry.system);
            }
        }
        if (phase == UpdatePhase::Actors)
        {
            // 止めはアクターの Update の中で始まるので、1 体ごとに問い直す
            m_actors.UpdateActors([this](const SceneActor* actor) noexcept { return UpdatesWhileHeld(actor); });
        }
        if (phase == UpdatePhase::RenderPrep)
        {
            m_actors.PrepareRenderActors();
        }
        // 管理役は段の SceneActor が出した物を受けてまとめるので、SceneActor の後に回す
        for (SceneSystem* system : m_scheduledSystems)
        {
            // 段の途中で外した物と、段を替えた物は呼ばない
            const bool stillRegistered =
                std::any_of(m_systems.begin(), m_systems.end(), [system, phase](const SystemEntry& entry) noexcept {
                    return entry.system == system && entry.phase == phase;
                });
            if (stillRegistered)
            {
                system->OnTick();
            }
        }
        m_actors.EndPhase();
    }

    SceneCamera* Scene::MainCamera() noexcept
    {
        return &m_mainCamera;
    }
} // namespace NS
