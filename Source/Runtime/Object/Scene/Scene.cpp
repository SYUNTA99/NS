#include "Runtime/Object/Scene/Scene.h"

#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Object/Components/CameraBrain.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Platform/Clock.h"

#include <limits>

namespace NS::Obj
{
    Scene::Scene()
    {
        // 描くには実カメラが 1 個要る。配置物ではないがシーンには必ず居るので、ここで ObjectList へ入れる
        // 保存・凍結・編集 UI に出ない一時オブジェクトで、データからの組み直しも跨いで残る
        // 描画 component は積まない。RegisterRenderable は virtual で、基底コンストラクタからは派生へ落ちない
        auto host = std::make_unique<GameObject>();
        CameraComponent* camera = host->AddComponent<CameraComponent>();
        camera->SetUp({0.0f, 1.0f, 0.0f});
        m_brain = host->AddComponent<NS::Obj::CameraBrain>();
        SpawnTransient(std::move(host));
    }

    Scene::~Scene() = default;

    void Scene::LoadFromData(SceneData&& data)
    {
        m_environment = data.environment;
        // 組む前に番号を揃える。未採番のまま組むと id で名指しできない実体ができる
        EnsureUniqueObjectIds(data);
        RebuildObjectsFrom(data);
    }

    void Scene::SyncPhysics()
    {
        // ギズモで動いた live の当たりを張り直す。object を作り直さないので選択・参照はそのまま保たれる
        m_objects.SyncPhysics(m_physicsScene);
        OnObjectsRebuilt();
        NotifyTransientsObjectsRebuilt();
    }

    void Scene::NotifyTransientsObjectsRebuilt()
    {
        for (GameObject* obj : m_objects)
        {
            if (obj->IsTransient())
            {
                obj->OnObjectsRebuilt();
            }
        }
    }

    const SceneData& Scene::BeginPlayBaseline()
    {
        // プレイ規則の判定と編集復帰の姿はこの凍結を読む。シミュレーションが動かした値は映らず、編集へ持ち込まれない
        if (!m_playBaselineInjected)
        {
            m_playBaseline = CaptureLiveToSceneData();
        }
        return m_playBaseline;
    }

    void Scene::SetPlayBaselineForTest(SceneData data)
    {
        m_playBaseline = std::move(data);
        m_playBaselineInjected = true;
    }

    void Scene::WritePlayBaselineField(const Component& comp, std::string_view fieldName)
    {
        const GameObject* owner = comp.Owner();
        if (owner == nullptr)
        {
            return;
        }

        const std::size_t objectIndex = FindObjectIndexById(m_playBaseline, owner->Id());
        if (objectIndex == k_NoObjectIndex)
        {
            return;
        }

        ObjectData& object = m_playBaseline.objects[objectIndex];

        for (nlohmann::json& entry : object.components)
        {
            if (ComponentEntryId(entry) != comp.Id())
            {
                continue;
            }
            const nlohmann::json serialized = SerializeComponent(comp);
            const auto fieldsIt = serialized.find("fields");
            if (fieldsIt == serialized.end())
            {
                return;
            }
            const auto valueIt = fieldsIt->find(std::string(fieldName));
            if (valueIt == fieldsIt->end())
            {
                return;
            }

            entry["fields"][std::string(fieldName)] = *valueIt;
            return;
        }
    }

    void Scene::SetSimulationEnabled(bool enabled) noexcept
    {
        m_simulationEnabled = enabled;
        m_simulationPaused = false;
        m_simulationStepFrames = 0;
    }

    void Scene::StepSimulation() noexcept
    {
        m_simulationPaused = true;
        m_simulationStepFrames += 1;
    }

    GameObject* Scene::SpawnTransient(std::unique_ptr<GameObject> obj)
    {
        if (!obj)
        {
            return nullptr;
        }

        obj->SetTransient(true);
        obj->AttachScene(this);
        GameObject* raw = m_objects.Append(std::move(obj));
        if (raw == nullptr)
        {
            return nullptr;
        }
        // データ由来の配置物は ObjectBuilder が引き当てる。後から入る一時オブジェクトはここで引き当てる
        // AssetManager が無い間は跳ばす。テストは資産なしでシーンを立てる
        if (m_assets != nullptr)
        {
            for (Component* comp : raw->Components())
            {
                if (comp != nullptr)
                {
                    comp->ResolveAssets(*m_assets);
                }
            }
        }
        // 開始は引き当ての後。OnStart の中で資産を読む Component が空の参照を掴まない
        raw->OnStart();
        return raw;
    }

    SceneData Scene::CaptureLiveToSceneData() const
    {
        // 一時オブジェクトを除く全 object を、全 component 値まで忠実に写す
        SceneData data{};
        data.environment = m_environment;
        data.nextObjectId = m_objects.NextObjectId();

        data.objects.reserve(m_objects.ObjectCount());
        for (const GameObject* obj : m_objects)
        {
            if (obj->IsTransient())
            {
                continue;
            }

            ObjectData od = CaptureObjectData(*obj);
            od.objectId = obj->Id();
            data.objects.push_back(std::move(od));
        }
        return data;
    }

    void Scene::DestroyObject(std::uint32_t objectId)
    {
        m_objects.RemoveByObjectId(objectId);
    }

    void Scene::RebuildObjectsFrom(const SceneData& data)
    {
        // GameObject の型選択は登録一覧、参照の実体化は各 component の ResolveAssets が行う
        // vcam の brain への付け外しは VirtualCamera が OnStart / OnEndPlay で自分で行う
        m_objects.Rebuild(data, *this, [this](const ObjectData& entry) { return BuildSceneObject(entry, m_assets); });
        m_objects.SyncPhysics(m_physicsScene);

        OnObjectsRebuilt();
        NotifyTransientsObjectsRebuilt();
    }

    void Scene::OnUpdate()
    {
        // 補間描画用。全配置物の Root を Snapshot する
        m_objects.SnapshotObjects();

        // 世界の駆動。読み込んだら回り続けるのが既定で、編集モードのエディタだけが止める
        // 時間停止中は上の snapshot だけが残り、previous == current で補間が凍る
        if (!m_simulationEnabled)
        {
            return;
        }

        if (m_simulationPaused)
        {
            if (m_simulationStepFrames <= 0)
            {
                return;
            }
            m_simulationStepFrames -= 1;
        }
#if !defined(NS_SHIPPING)
        // 描画 1 回ごとに捨てると、その間に進む固定ステップの回数で映る図形が変わる
        NS::Gfx::DebugDraw::BeginStep();
#endif
        // カメラが追う前に物理を進める。自機と衝突の裁定は Update 帯までに終わっている
        m_objects.UpdateObjects(std::numeric_limits<int>::min(), TickPriority::LateUpdate);
        // RigidBody は物理の前後に挟む。Update 帯が動かしたキネマティックを運び、動いた body をカメラが追う前に書き戻す
        m_objects.ForEachComponent<RigidBody>([](RigidBody& body) {
            if (body.IsActive())
            {
                body.PrePhysicsStep();
            }
        });
        m_physicsScene.Update(NS::Platform::FrameTimer::FixedDelta());
        m_objects.ForEachComponent<RigidBody>([](RigidBody& body) {
            if (body.IsActive())
            {
                body.PostPhysicsStep();
            }
        });
        m_objects.UpdateObjects(TickPriority::LateUpdate);
        // 時間停止中は凍らせる。停止の判定より後ろ
        m_sceneRenderer.UpdateEffects(NS::Platform::FrameTimer::FixedDelta());
    }

    void Scene::OnShutdown()
    {
        m_objects.Clear();
        // host も ObjectList と一緒に消えた。控えを残すと破棄済みを指し続ける
        m_brain = nullptr;
    }

    NS::Gfx::RenderSettings Scene::ResolveSceneSettings(const NS::Gfx::RenderSettings& projectDefaults)
    {
        return m_sceneRenderer.ResolveSceneSettings(projectDefaults);
    }

    void Scene::OnRender()
    {
        // 更新は終わっているので bounds は 1 フレームに 1 回で足りる。ビューを何枚描いても同じ値
        m_sceneRenderer.SyncRenderBounds();
        OnRenderScene();
    }

    void Scene::RegisterRenderable(IRenderable* renderable)
    {
        m_sceneRenderer.RegisterRenderable(renderable);
    }

    void Scene::UnregisterRenderable(IRenderable* renderable)
    {
        m_sceneRenderer.UnregisterRenderable(renderable);
    }

    void Scene::RegisterOverlay(OverlayRenderer* overlay)
    {
        m_sceneRenderer.RegisterOverlay(overlay);
    }

    void Scene::UnregisterOverlay(OverlayRenderer* overlay)
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

    void Scene::DrawOpaque(const NS::Gfx::RenderContext& context)
    {
        m_sceneRenderer.DrawOpaque(context);
    }

    void Scene::DrawTransparent(const NS::Gfx::RenderContext& context)
    {
        m_sceneRenderer.DrawTransparent(context);
    }

    void Scene::DrawOverlays(const NS::Gfx::RenderContext& context)
    {
        m_sceneRenderer.DrawOverlays(context);
    }

    NS::Obj::CameraBrain* Scene::CameraBrain() noexcept
    {
        return m_brain;
    }

    CameraComponent* Scene::MainCamera() noexcept
    {
        if (m_brain == nullptr)
        {
            return nullptr;
        }
        return m_brain->Camera();
    }

    void Scene::OnRenderScene()
    {
        NS::Obj::CameraBrain* brain = CameraBrain();
        CameraComponent* camera = MainCamera();
        if (brain == nullptr || camera == nullptr)
        {
            return;
        }
        m_sceneRenderer.Render(*brain, *camera, m_environment);
    }
} // namespace NS::Obj
