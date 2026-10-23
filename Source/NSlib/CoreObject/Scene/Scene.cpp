#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Types.h"
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
#include "NSlib/Windows/Clock.h"

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
        // Jolt は物理の段に置いた個体の後で進む
        AddSystemTick(m_physics, UpdatePhase::Physics);
        // 当たりの調べ役はセンサーの段 (物理の後、仕掛けとゴールの前) で回る
        AddSystemTick(m_hitSensors, UpdatePhase::Sensors);
        // 管理役は vcam を動かす追従カメラの後で選び直す
        AddSystemTick(m_cameraManager, UpdatePhase::Camera);
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
            objects.push_back(ActorToJson(*obj));
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

    const nlohmann::json& Scene::BeginPlayBaseline()
    {
        // プレイ規則の判定と編集復帰の姿はこの凍結を読む。シミュレーションが動かした値は映らず、編集へ持ち込まれない
        m_playBaseline = ToJson();
        return m_playBaseline;
    }

    void Scene::WritePlayBaselineField(const SceneActor& actor, const ClassObjInfo& row, std::string_view fieldName)
    {
        const std::size_t index = FindActorIndexById(m_playBaseline, actor.Id());
        if (index == k_NoActorIndex)
        {
            return;
        }
        const Object* part = row.get(&actor);
        if (part == nullptr)
        {
            return;
        }
        // .actor へ書く欄は凍結が持たない。写すと編集へ戻る時に配置物の値として捨てられる
        const FieldDesc* field = FindField(part->GetReflection(), fieldName);
        if (field == nullptr || FieldDestinationOf(actor, *part, *field) != FieldDestination::Scene)
        {
            return;
        }
        nlohmann::json& frozenParts = ActorJsonClassObjs(SceneJsonActors(m_playBaseline)[index]);
        // 上書きの一覧から外した項目は、凍結からも消す。残すと編集へ戻る時に上書きとして読み直される
        if (part != &actor.Root() && field->type != FieldType::ActorRef && !actor.IsSceneOverride(row.key, fieldName))
        {
            const nlohmann::json::iterator frozen = frozenParts.find(row.key);
            if (frozen != frozenParts.end())
            {
                frozen->erase(std::string{fieldName});
            }
            return;
        }
        const nlohmann::json fields = SerializeClassObjFields(*part);
        const nlohmann::json::const_iterator value = fields.find(std::string{fieldName});
        if (value == fields.end())
        {
            return;
        }
        frozenParts[std::string{row.key}][std::string{fieldName}] = *value;
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

    void Scene::HoldOthers(int steps) noexcept
    {
        if (steps <= 0)
        {
            m_othersHeldSteps = 0;
            return;
        }
        m_othersHeldSteps = std::max(m_othersHeldSteps, steps);
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
        StartSpawned(*raw);
        return raw;
    }

    void Scene::StartSpawned(SceneActor& obj)
    {
        // 素材は作る関数の Init が読み終えている
        obj.OnStart();
        // 後から入る物は他の配置物が既に揃っている
        obj.InitAfterPlacement();
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
        // 同じ id の物は 1 つだけ持つ。戻しの控えを何度当てても保存の配置物は増えない
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
        StartSpawned(*raw);
        return raw;
    }

    SceneActor* Scene::ReplaceFromJson(const nlohmann::json& actorJson)
    {
        const uint32 id = ActorJsonId(actorJson);
        const std::size_t index = m_actors.IndexOfActorId(id);
        SceneActor* old = m_actors.FindByActorId(id);
        if (old == nullptr)
        {
            return SpawnFromJson(actorJson);
        }

        std::unique_ptr<SceneActor> built = BuildOrKeepUnresolved(actorJson);
        // 組めない姿は居ない姿として扱い、JSON は BuildOrKeepUnresolved が持つ。
        // RemoveActor は持った JSON も捨てるので、アクターだけを外す
        m_actors.RemoveByActorId(id);
        if (!built)
        {
            return nullptr;
        }
        SceneActor* raw = m_actors.InsertFromJson(std::move(built), actorJson, index);
        if (raw == nullptr)
        {
            return nullptr;
        }
        StartSpawned(*raw);
        return raw;
    }

    SceneActor* Scene::ApplyFromJson(const nlohmann::json& snapshot)
    {
        SceneActor* obj = m_actors.FindByActorId(ActorJsonId(snapshot));
        if (obj == nullptr)
        {
            return SpawnFromJson(snapshot);
        }
        // 同じクラスでも .actor が違えば敷く既定が違うので、作り直す
        if (ActorJsonFile(snapshot) != obj->ActorFileName())
        {
            return ReplaceFromJson(snapshot);
        }
        // 控えはシーンの上書きだけを持つ。上書きの無い項目も .actor の値へ戻すため、全部の値に重ねてから写す
        const nlohmann::json* values = ActorFileLibrary::Get().Values(obj->ActorFileName());
        if (values == nullptr)
        {
            return ReplaceFromJson(snapshot);
        }
        const std::optional<nlohmann::json> merged = MergeLayer(*values, snapshot, FieldDestination::Scene, *obj);
        if (!merged.has_value())
        {
            return ReplaceFromJson(snapshot);
        }
        const nlohmann::json& object = *merged;
        m_actors.ApplyIdentity(*obj, ReadActorHeader(snapshot));
        ReadSceneOverrides(*obj, snapshot);
        bool written = false;
        ForEachReflectedClassObj(*obj, [&](const ClassObjInfo& field, Object* classObj) {
            const nlohmann::json* fields = ClassObjFields(object, field.key);
            if (fields == nullptr || SerializeClassObjFields(*classObj) == *fields)
            {
                return;
            }
            (void)ApplyJsonFields(*classObj, *fields);
            written = true;
        });
        // 書き換えた欄に参照があれば読み直す。作る時と同じ組を通る
        if (written)
        {
            obj->ReloadAssets();
        }
        return obj;
    }

    void Scene::RemoveActor(uint32 objectId)
    {
        m_actors.RemoveByActorId(objectId);
        std::erase_if(m_unresolvedActors, [objectId](const nlohmann::json& unresolved) {
            return ActorJsonId(unresolved) == objectId;
        });
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
        // シーンに 1 つの物 (進行役など) は配置物と画面の一覧を借りるので先に捨てる。
        // 組み直しでは、組み直した配置物の開始が要る物が最初の状態から作り直す
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
        // 仮想カメラの付け外しは、持ち主の SceneActor が OnStart / OnEndPlay で行う
        // .actor の見つからない配置物は作らず、保存で消さないように JSON のまま持つ
        m_actors.RebuildFromJson(
            scene, *this, [this](const nlohmann::json& entry) { return BuildOrKeepUnresolved(entry); });
        // 当たりは配置物の開始で入っている。まとめて入れた後の broadphase だけを張り直す
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
            // 補間描画用。全配置物の根の Transform と Model の前の値を控える
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
            // 止めは自機の段で置かれるので、毎段読み直してその歩の残りの段から効かせる
            if (m_othersHeldSteps > 0 && !RunsWhileOthersHeld(phase))
            {
                continue;
            }
            ExecutePhase(phase);
        }
        m_actors.RemoveDestroyed();
        // 止めは世界を進めた歩で数える。遅い世界でも、止めた歩の数だけ自機が進む
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

    void Scene::RegisterRenderable(IRenderable* renderable)
    {
        m_sceneRenderer.RegisterRenderable(renderable);
    }

    void Scene::UnregisterRenderable(IRenderable* renderable)
    {
        m_sceneRenderer.UnregisterRenderable(renderable);
    }

    void Scene::RegisterOverlay(IOverlay* overlay)
    {
        m_sceneRenderer.RegisterOverlay(overlay);
    }

    void Scene::UnregisterOverlay(IOverlay* overlay)
    {
        m_sceneRenderer.UnregisterOverlay(overlay);
    }

    void Scene::RegisterLight(DirectionalLight* light)
    {
        m_sceneRenderer.RegisterLight(light);
    }

    void Scene::UnregisterLight(DirectionalLight* light)
    {
        m_sceneRenderer.UnregisterLight(light);
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
        m_actors.ExecutePhase(phase);
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
