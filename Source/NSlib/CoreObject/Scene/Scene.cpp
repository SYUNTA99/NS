#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
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
        const SceneEnvironment environment = ReadSceneEnvironment(scene);
        SetSkyboxPath(environment.skybox);
        // 組む前に番号を揃える。未採番のまま組むと id で名指しできない実体ができる
        EnsureUniqueObjectIds(scene);
        RebuildSceneFrom(scene);
    }

    nlohmann::json Scene::ToJson() const
    {
        nlohmann::json scene = MakeSceneJson();
        WriteSceneEnvironment(scene, m_environment);
        SetSceneJsonNextObjectId(scene, m_objects.NextObjectId());
        nlohmann::json& objects = SceneJsonObjects(scene);
        for (const SceneActor* obj : m_objects)
        {
            objects.push_back(ObjectToJson(*obj));
        }
        for (const nlohmann::json& object : m_unresolvedObjects)
        {
            objects.push_back(object);
        }
        return scene;
    }

    void Scene::SyncPhysics()
    {
        // ギズモで動いた実体の当たりを張り直す。object を作り直さないので選択・参照はそのまま保たれる
        m_objects.SyncPhysics(m_physics.Physics());
    }

    const nlohmann::json& Scene::BeginPlayBaseline()
    {
        // プレイ規則の判定と編集復帰の姿はこの凍結を読む。シミュレーションが動かした値は映らず、編集へ持ち込まれない
        m_playBaseline = ToJson();
        return m_playBaseline;
    }

    void Scene::WritePlayBaselineField(const SceneActor& actor, const ClassObjInfo& row, std::string_view fieldName)
    {
        const std::size_t index = FindObjectIndexById(m_playBaseline, actor.Id());
        if (index == k_NoObjectIndex)
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
        if (field == nullptr || FieldDestinationOf(actor, *part, *field) == FieldDestination::ActorFile)
        {
            return;
        }
        const nlohmann::json fields = SerializeClassObjFields(*part);
        const nlohmann::json::const_iterator value = fields.find(std::string{fieldName});
        if (value == fields.end())
        {
            return;
        }
        ObjectJsonClassObjs(SceneJsonObjects(m_playBaseline)[index])[std::string{row.key}][std::string{fieldName}] =
            *value;
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

    SceneActor* Scene::AdoptObject(std::unique_ptr<SceneActor> obj, std::string name)
    {
        if (!obj)
        {
            return nullptr;
        }

        obj->AttachScene(this);
        SceneActor* raw = m_objects.AppendWithNewId(std::move(obj), std::move(name));
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

    SceneActor* Scene::SpawnFromJson(const nlohmann::json& object)
    {
        std::unique_ptr<SceneActor> built = ObjectFromJson(object, *this);
        if (!built)
        {
            return nullptr;
        }
        built->AttachScene(this);
        SceneActor* raw = m_objects.InsertFromJson(std::move(built), object, m_objects.ObjectCount());
        if (raw == nullptr)
        {
            return nullptr;
        }
        StartSpawned(*raw);
        return raw;
    }

    SceneActor* Scene::ReplaceFromJson(const nlohmann::json& object)
    {
        const uint32 id = ObjectJsonId(object);
        const std::size_t index = m_objects.IndexOfObjectId(id);
        SceneActor* old = m_objects.FindByObjectId(id);
        if (old == nullptr)
        {
            return SpawnFromJson(object);
        }

        std::unique_ptr<SceneActor> built = ObjectFromJson(object, *this);
        if (!built)
        {
            // 組める部品が無い姿は、居ない姿として扱う
            DestroyObject(id);
            return nullptr;
        }

        DestroyObject(id);
        built->AttachScene(this);
        SceneActor* raw = m_objects.InsertFromJson(std::move(built), object, index);
        if (raw == nullptr)
        {
            return nullptr;
        }
        StartSpawned(*raw);
        return raw;
    }

    SceneActor* Scene::ApplyFromJson(const nlohmann::json& snapshot)
    {
        SceneActor* obj = m_objects.FindByObjectId(ObjectJsonId(snapshot));
        if (obj == nullptr)
        {
            return SpawnFromJson(snapshot);
        }
        // 同じクラスでも .actor が違えば敷く既定が違うので、作り直す
        if (ObjectJsonActorFile(snapshot) != obj->ActorFileName())
        {
            return ReplaceFromJson(snapshot);
        }
        // 控えは個体の上書きだけを持つ。上書きの無い欄も .actor の値へ戻すため、全欄の姿へ広げてから比べる
        const nlohmann::json object = ExpandObjectJson(snapshot);
        bool same = true;
        std::size_t classObjCount = 0;
        ForEachReflectedClassObj(*obj, [&](const ClassObjInfo& field, Object*) {
            ++classObjCount;
            if (ClassObjFields(object, field.key) == nullptr)
            {
                same = false;
            }
        });
        if (!same || classObjCount != ObjectJsonClassObjs(object).size())
        {
            return ReplaceFromJson(snapshot);
        }
        m_objects.ApplyIdentity(*obj, ReadObjectHeader(object));
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

    void Scene::DestroyObject(uint32 objectId)
    {
        m_objects.RemoveByObjectId(objectId);
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
        m_objects.Clear();
        m_unresolvedObjects.clear();
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
        m_objects.RebuildFromJson(scene, *this, [this](const nlohmann::json& entry) {
            std::unique_ptr<SceneActor> built = ObjectFromJson(entry, *this);
            if (!built)
            {
                m_unresolvedObjects.push_back(entry);
            }
            return built;
        });
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
            m_objects.SnapshotObjects();
            m_objects.SyncCollisions();
            return;
        }

        if (m_simulationPaused)
        {
            if (m_simulationStepFrames <= 0)
            {
                m_objects.SnapshotObjects();
                m_objects.SyncCollisions();
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
            m_objects.SnapshotObjects();
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
        m_objects.RemoveDestroyed();
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
        m_sceneRenderer.Render(m_cameraManager, m_mainCamera, m_ui, m_fade.Fade(), m_environment.skybox, alpha);
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
        m_objects.BeginPhase();
        // 段で呼ぶ管理役は SceneActor を回す前に決める。段の SceneActor が足した管理役はこの段では呼ばない
        m_scheduledSystems.clear();
        for (const SystemEntry& entry : m_systems)
        {
            if (entry.phase == phase)
            {
                m_scheduledSystems.push_back(entry.system);
            }
        }
        m_objects.ExecutePhase(phase);
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
        m_objects.EndPhase();
    }

    SceneCamera* Scene::MainCamera() noexcept
    {
        return &m_mainCamera;
    }
} // namespace NS
