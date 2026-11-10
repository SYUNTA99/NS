#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Core/Assert.h"
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

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace NS
{
    Scene::Scene()
    {
        m_mainCamera.SetUp({0.0f, 1.0f, 0.0f});
        m_cameraManager.SetCamera(&m_mainCamera);
        m_cameraManager.SetCollision(this);
    }

    Scene::~Scene()
    {
        // 下見のシーンは OnShutdown を呼ばずに壊す。メンバーが残っているうちにアクターの OnEndPlay を回し、図形も消す
        OnShutdown();
    }

    void Scene::StartPlay()
    {
        NS_ASSERT(Scene, !m_playing, "StartPlay: ResetFromJson を通らずに始めた");
        m_playing = true;
        OnStartPlay();
    }

    void Scene::ResetFromJson(nlohmann::json scene)
    {
        // 組み直した世界へ、派生が持つ前のプレイの状態を持ち込まない
        if (m_playing)
        {
            m_playing = false;
            OnStopPlay();
        }
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

    void Scene::SetEnvironment(const SceneEnvironment& environment) noexcept { m_environment = environment; }

    void Scene::SetSimulationEnabled(bool enabled) noexcept
    {
        m_simulationEnabled = enabled;
        m_simulationPaused = false;
        m_simulationStepFrames = 0;
        CancelStopScene();
    }

    void Scene::StepSimulation() noexcept
    {
        m_simulationPaused = true;
        m_simulationStepFrames += 1;
    }

    void Scene::RequestStopScene(const SceneActor* mover, int steps) noexcept
    {
        NS_ASSERT(Scene, mover != nullptr, "Scene::RequestStopScene: 止め始めるアクターが無い");
        // 止めている間は先に止め始めたアクターが持つ。後から頼んだアクターに替えると、止め始めたアクターが止まる
        if (m_stopSceneSteps > 0 && !IsStopSceneMover(mover))
        {
            return;
        }
        const uint32 moverId = mover->Id();
        // 番地と id の両方を比べる理由は IsStopSceneMover と同じ。比べる相手が止め始めたアクターでなく、頼んだアクターなので分ける
        const bool sameRequester =
            m_stopSceneRequested && mover == m_stopSceneRequester && moverId == m_stopSceneRequesterId;
        // 同じ歩に先に止めを頼んだアクターが持つ。解く頼みは止めを持たないので、後から頼んだ止めで置き換える
        if (m_stopSceneRequested && !sameRequester && m_requestedStopSceneSteps > 0)
        {
            return;
        }
        if (steps <= 0)
        {
            m_requestedStopSceneSteps = 0;
        }
        else if (sameRequester)
        {
            m_requestedStopSceneSteps = std::max(m_requestedStopSceneSteps, steps);
        }
        else
        {
            m_requestedStopSceneSteps = steps;
        }
        m_stopSceneRequested = true;
        m_stopSceneRequester = mover;
        m_stopSceneRequesterId = moverId;
    }

    void Scene::ApplyStopSceneRequest() noexcept
    {
        if (!m_stopSceneRequested)
        {
            return;
        }
        m_stopSceneRequested = false;
        // 止めている間に頼めるのは止め始めたアクターだけ (RequestStopScene が断る) なので、ここでは比べない
        if (m_requestedStopSceneSteps <= 0)
        {
            m_stopSceneSteps = 0;
            return;
        }
        m_stopSceneSteps = std::max(m_stopSceneSteps, m_requestedStopSceneSteps);
        m_stopSceneMover = m_stopSceneRequester;
        m_stopSceneMoverId = m_stopSceneRequesterId;
    }

    void Scene::CancelStopScene() noexcept
    {
        m_stopSceneSteps = 0;
        m_stopSceneRequested = false;
    }

    bool Scene::IsStopSceneMover(const SceneActor* actor) const noexcept
    {
        // id の無い JSON から読んだアクターは id が 0 で重なるので、番地も比べる。
        // 止め始めたアクターは止めの最中だけ読む。最中の m_stopSceneMover は nullptr でないので、nullptr は読まずに外れる
        return m_stopSceneSteps > 0 && actor == m_stopSceneMover && actor->Id() == m_stopSceneMoverId;
    }

    bool Scene::IsStopSceneRequester(const SceneActor* actor) const noexcept
    {
        // 番地と id の両方を比べる理由は IsStopSceneMover と同じ。頼みを控えている間の m_stopSceneRequester は nullptr でない
        return m_stopSceneRequested && m_requestedStopSceneSteps > 0 && actor == m_stopSceneRequester &&
               actor->Id() == m_stopSceneRequesterId;
    }

    float Scene::RenderAlpha(float frameAlpha) const noexcept
    {
        if (!m_simulationEnabled || m_simulationPaused)
        {
            return 1.0f;
        }
        return frameAlpha;
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
        obj->Start();
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
        const ActorHeader header = ReadActorHeader(snapshot);
        m_actors.ApplyIdentityExceptPresence(obj, header);
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
        if (actorFileChanged)
        {
            obj->ReloadActorFileOnlyValues();
        }
        // 書き換えた欄に参照があれば読み直す。作る時と同じ組を通る
        if (written || actorFileChanged)
        {
            obj->ReloadAssets();
        }
        // 出す時は今の姿で当たりと補間の前の値を揃えるので、JSON の値を全部当ててから出す
        ActorList::ApplyPresence(obj, header);
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
        // 前の世界のアクターが頼んだ演出と振動を、次の世界へ持ち越さない
        m_screenFlash.Clear();
        ClearViewEffects();
        m_padRumble.Clear();
        Fade().Cancel();
        m_actors.Clear();
        m_unresolvedActors.clear();
        ClearEffects();
#if !defined(NS_SHIPPING)
        // 図形は前の世界の姿で、溜め場は静的な 1 つ。残すと、次の歩が来るまで消えた物の線が出る
        Gfx::ClearDebugDraw();
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
        m_physics.OptimizeBroadPhase();
    }

    void Scene::OnUpdate()
    {
        // 前の歩で頼まれた止めをここで効かせる。歩の途中では止めが変わらないので、状態ごとの関数の中で確かめ直さない
        ApplyStopSceneRequest();
        // 世界の駆動。読み込んだら回り続けるのが既定で、編集モードのエディタだけが止める
        // 時間停止中は前の値の控えだけが残り、previous == current で補間が凍る
        // Actor の Update が回らない歩も、編集で動いた当たりを揃える
        if (!m_simulationEnabled)
        {
            m_actors.SnapshotActors();
            m_actors.SyncCollisions();
            m_actors.SyncShadows();
            return;
        }

        if (m_simulationPaused)
        {
            if (m_simulationStepFrames <= 0)
            {
                m_actors.SnapshotActors();
                m_actors.SyncCollisions();
                m_actors.SyncShadows();
                return;
            }
            // コマ送りは実時間の 1 歩
            m_simulationStepFrames -= 1;
        }

        // 補間描画用。全アクターの根の Transform と Model の前の値を控える
        m_actors.SnapshotActors();
#if !defined(NS_SHIPPING)
        // 描画 1 回ごとに捨てると、その間に進む固定ステップの回数で映る図形が変わる
        Gfx::BeginDebugDrawStep();
#endif
        // 歩の間はアクターを解放しない。後で呼ぶ予定のアクターの生ポインタが破棄済みにならない
        m_actors.BeginStep();
        if (IsSceneStopped())
        {
            StepStopped();
        }
        else
        {
            StepWorld();
        }
        m_actors.EndStep();
        m_actors.RemoveDestroyed();
        // 影は、動く床・物理・ゲームの流れの瞬間移動まで全部動いた後の地面を探す。歩の途中で出た物もここで探す
        m_actors.SyncShadows();
        // 歩の途中で CancelStopScene が解いた時は数えない
        if (m_stopSceneSteps > 0)
        {
            m_stopSceneSteps -= 1;
        }
    }

    void Scene::StepWorld()
    {
        m_actors.UpdateActors();
        // Jolt の broadphase の木は、アクターが全部動いた後で組み直す
        m_physics.Update(OS::FrameTimer::FixedDelta());
        m_hitSensors.OnTick();
        // ゲームの流れは当たりの判定の後で、同じ歩に届いた知らせを読む
        StepGame();
        StepView();
    }

    void Scene::StepStopped()
    {
        m_actors.UpdateActors();
        StepViewStopped();
    }

    void Scene::StepView()
    {
        StepViewStopped();
        // エフェクトの世界は同じ歩のアクターが出した演出を受けてから進む
        m_sceneRenderer.OnTick();
    }

    void Scene::StepViewStopped()
    {
        // 仮想カメラはアクターと StepGame が動かした後の相手を追い、続けて選び直す
        m_cameraManager.OnTick();
        m_ui.OnTick();
        m_fade.Advance(OS::FrameTimer::FixedDelta());
        // 画面の演出と振動は、アクターの PrepareRender の後に進める
        m_actors.PrepareRenderActors();
        m_screenFlash.OnTick();
        StepViewEffects();
        m_padRumble.OnTick();
    }

    void Scene::OnShutdown()
    {
        ClearWorld();
        m_cameraManager.SetCamera(nullptr);
    }

    void Scene::OnRender()
    {
        // 世界が実時間で進まない間は、前の固定フレームからの経過の割合に意味が無い
        // 描く度に割合が変わると、同じフレームの絵が揺れる
        const float alpha = RenderAlpha(OS::FrameTimer::Alpha());
        // 姿勢はビューに依らないので 1 フレームに 1 回。止めている間の実カメラは止めた側が書く
        if (m_simulationEnabled)
        {
            m_cameraManager.Evaluate(alpha);
        }
        m_sceneRenderer.Render(m_mainCamera, m_ui, m_fade, m_environment.skybox, alpha);
    }

    SceneRenderer* Scene::GetSceneRenderer() const noexcept
    {
        return const_cast<SceneRenderer*>(&m_sceneRenderer);
    }

    CameraManager* Scene::GetCameraManager() const noexcept
    {
        return const_cast<CameraManager*>(&m_cameraManager);
    }

    Phys::PhysicsScene* Scene::GetPhysicsScene() const noexcept
    {
        // 地形を問う関数のほか、Collision も持ち主の Scene の body を出し入れする時にここから引く
        return const_cast<Phys::PhysicsScene*>(&m_physics);
    }

    Gfx::EffectScene* Scene::GetEffectScene() const noexcept { return m_sceneRenderer.Effects(); }

    SceneCamera* Scene::MainCamera() noexcept { return &m_mainCamera; }
} // namespace NS
