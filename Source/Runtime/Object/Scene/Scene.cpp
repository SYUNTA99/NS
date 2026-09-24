#include "Runtime/Object/Scene/Scene.h"

#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Object/Components/CameraBrain.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Platform/Clock.h"

#include <limits>
#include <string_view>
#include <vector>

namespace NS::Obj
{
    namespace
    {
        // obj の component のうち JSON へ写る物 (リフレクションを持つ物) を並び順に集める
        [[nodiscard]] std::vector<Component*> ReflectedComponents(const GameObject& obj)
        {
            std::vector<Component*> reflected;
            reflected.reserve(obj.Components().size());
            for (Component* comp : obj.Components())
            {
                if (comp != nullptr && comp->GetReflection() != nullptr)
                {
                    reflected.push_back(comp);
                }
            }
            return reflected;
        }

        // 実体を残したまま値だけ写せるか。クラスと component の並び・型・id が JSON と一致する時だけ真
        // 一致しない姿は component の増減か入れ替えで、兄弟を開始時に掴む component があるため作り直す
        [[nodiscard]] bool MatchesStructure(const GameObject& obj,
                                            const std::vector<Component*>& reflected,
                                            const nlohmann::json& object)
        {
            if (std::string_view(obj.ClassName()) != ObjectJsonClass(object))
            {
                return false;
            }
            const nlohmann::json& components = ObjectJsonComponents(object);
            if (components.size() != reflected.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < reflected.size(); ++i)
            {
                const nlohmann::json& entry = components[i];
                if (ComponentEntryId(entry) != reflected[i]->Id() ||
                    ComponentEntryType(entry) != std::string_view(reflected[i]->GetReflection()->typeName))
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    Scene::Scene()
    {
        // 描くには実カメラが 1 個要る。配置物ではないがシーンには必ず居るので、ここで ObjectList へ入れる
        // 保存・凍結・編集 UI に出ない一時オブジェクトで、データからの組み直しも跨いで残る
        // 描画 component は積まない。RegisterRenderable は virtual で、基底コンストラクタからは派生へ落ちない
        std::unique_ptr<GameObject> host = std::make_unique<GameObject>();
        CameraComponent* camera = host->AddComponent<CameraComponent>();
        camera->SetUp({0.0f, 1.0f, 0.0f});
        m_brain = host->AddComponent<NS::Obj::CameraBrain>();
        SpawnTransient(std::move(host));
    }

    Scene::~Scene() = default;

    void Scene::LoadJson(nlohmann::json scene)
    {
        m_skyboxPath = std::string{SceneJsonSkybox(scene)};
        // 組む前に番号を揃える。未採番のまま組むと id で名指しできない実体ができる
        EnsureUniqueObjectIds(scene);
        RebuildObjectsFrom(scene);
    }

    nlohmann::json Scene::ToJson() const
    {
        nlohmann::json scene = MakeSceneJson();
        SetSceneJsonSkybox(scene, m_skyboxPath);
        SetSceneJsonNextObjectId(scene, m_objects.NextObjectId());
        nlohmann::json& objects = SceneJsonObjects(scene);
        for (const GameObject* obj : m_objects)
        {
            // 一時オブジェクトは保存にも凍結にも写さない
            if (obj->IsTransient())
            {
                continue;
            }
            objects.push_back(ObjectToJson(*obj));
        }
        return scene;
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

    const nlohmann::json& Scene::BeginPlayBaseline()
    {
        // プレイ規則の判定と編集復帰の姿はこの凍結を読む。シミュレーションが動かした値は映らず、編集へ持ち込まれない
        if (!m_playBaselineInjected)
        {
            m_playBaseline = ToJson();
        }
        return m_playBaseline;
    }

    void Scene::SetPlayBaselineForTest(nlohmann::json scene)
    {
        m_playBaseline = std::move(scene);
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

        nlohmann::json& object = SceneJsonObjects(m_playBaseline)[objectIndex];

        for (nlohmann::json& entry : ObjectJsonComponents(object))
        {
            if (ComponentEntryId(entry) != comp.Id())
            {
                continue;
            }
            const nlohmann::json serialized = SerializeComponent(comp);
            const nlohmann::json::const_iterator fieldsIt = serialized.find("fields");
            if (fieldsIt == serialized.end())
            {
                return;
            }
            const nlohmann::json::const_iterator valueIt = fieldsIt->find(std::string(fieldName));
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
        StartSpawned(*raw);
        return raw;
    }

    GameObject* Scene::SpawnObject(std::unique_ptr<GameObject> obj, std::string name)
    {
        if (!obj)
        {
            return nullptr;
        }

        obj->AttachScene(this);
        GameObject* raw = m_objects.AppendWithNewId(std::move(obj), std::move(name));
        if (raw == nullptr)
        {
            return nullptr;
        }
        StartSpawned(*raw);
        return raw;
    }

    void Scene::StartSpawned(GameObject& obj)
    {
        // データ由来の配置物は ObjectBuilder が引き当てる。後から入る物はここで引き当てる
        // AssetManager が無い間は跳ばす。テストは資産なしでシーンを立てる
        if (m_assets != nullptr)
        {
            for (Component* comp : obj.Components())
            {
                if (comp != nullptr)
                {
                    comp->ResolveAssets(*m_assets);
                }
            }
        }
        // 開始は引き当ての後。OnStart の中で資産を読む Component が空の参照を掴まない
        obj.OnStart();
    }

    GameObject* Scene::SpawnFromJson(const nlohmann::json& object)
    {
        // 資産の引き当ては開始の直前に StartSpawned がまとめて行う
        std::unique_ptr<GameObject> built = ObjectFromJson(object, nullptr);
        if (!built)
        {
            return nullptr;
        }
        built->AttachScene(this);
        GameObject* raw = m_objects.InsertFromJson(std::move(built), object, m_objects.ObjectCount());
        if (raw == nullptr)
        {
            return nullptr;
        }
        if (GameObject* parent = m_objects.FindByObjectId(ObjectJsonParent(object)); parent != nullptr && parent != raw)
        {
            raw->SetParent(parent);
        }
        StartSpawned(*raw);
        return raw;
    }

    GameObject* Scene::ReplaceFromJson(const nlohmann::json& object)
    {
        const std::uint32_t id = ObjectJsonId(object);
        const std::size_t index = m_objects.IndexOfObjectId(id);
        GameObject* old = m_objects.FindByObjectId(id);
        if (old == nullptr)
        {
            return SpawnFromJson(object);
        }

        std::unique_ptr<GameObject> built = ObjectFromJson(object, nullptr);
        if (!built)
        {
            // 組める component が無い姿は、居ない姿として扱う
            DestroyObject(id);
            return nullptr;
        }

        // 子は古い方の破棄で根に落ちるので、先に控えて新しい方へ付け直す。local の姿はそのまま残る
        std::vector<std::uint32_t> childIds;
        childIds.reserve(old->Children().size());
        for (const GameObject* child : old->Children())
        {
            childIds.push_back(child->Id());
        }

        DestroyObject(id);
        built->AttachScene(this);
        GameObject* raw = m_objects.InsertFromJson(std::move(built), object, index);
        if (raw == nullptr)
        {
            return nullptr;
        }
        if (GameObject* parent = m_objects.FindByObjectId(ObjectJsonParent(object)); parent != nullptr && parent != raw)
        {
            raw->SetParent(parent);
        }
        for (const std::uint32_t childId : childIds)
        {
            if (GameObject* child = m_objects.FindByObjectId(childId))
            {
                child->SetParent(raw);
            }
        }
        StartSpawned(*raw);
        return raw;
    }

    GameObject* Scene::ApplyFromJson(const nlohmann::json& object)
    {
        GameObject* obj = m_objects.FindByObjectId(ObjectJsonId(object));
        if (obj == nullptr)
        {
            return SpawnFromJson(object);
        }
        const std::vector<Component*> reflected = ReflectedComponents(*obj);
        if (!MatchesStructure(*obj, reflected, object))
        {
            return ReplaceFromJson(object);
        }

        // 実体はそのまま。ポインタも実行時の状態も残し、JSON と違う値だけを書き戻す
        const std::string_view name = ObjectJsonName(object);
        if (!name.empty() && obj->Name() != name)
        {
            m_objects.RenameObject(*obj, name);
        }
        obj->SetActive(ObjectJsonActive(object));

        // 親の付け替えは local の値を保つ。transform は後で JSON の local を写すので、親を先に戻す
        GameObject* parent = m_objects.FindByObjectId(ObjectJsonParent(object));
        if (parent == obj)
        {
            parent = nullptr;
        }
        if (obj->Parent() != parent)
        {
            obj->SetParent(parent);
        }

        const nlohmann::json& components = ObjectJsonComponents(object);
        for (std::size_t i = 0; i < reflected.size(); ++i)
        {
            Component& comp = *reflected[i];
            const nlohmann::json& entry = components[i];

            const std::string_view compName = ComponentEntryName(entry);
            if (!compName.empty() && comp.Name() != compName)
            {
                obj->RenameComponent(comp, compName);
            }
            comp.SetEnabled(ComponentEntryEnabled(entry));

            const nlohmann::json::const_iterator fieldsIt = entry.find("fields");
            if (fieldsIt == entry.end())
            {
                continue;
            }
            // 値の同じ component は触らない。資産の引き直しで実行時の状態 (再生位置など) を失わせない
            const nlohmann::json current = SerializeComponent(comp);
            const nlohmann::json::const_iterator currentIt = current.find("fields");
            if (currentIt != current.end() && *currentIt == *fieldsIt)
            {
                continue;
            }
            (void)ApplyJsonFields(comp, *fieldsIt);
            // 参照文字列が変わっていれば実体も差し替える。AssetManager が無い間 (テスト) は跳ばす
            if (m_assets != nullptr)
            {
                comp.ResolveAssets(*m_assets);
            }
        }
        return obj;
    }

    void Scene::DestroyObject(std::uint32_t objectId)
    {
        m_objects.RemoveByObjectId(objectId);
    }

    void Scene::RebuildObjectsFrom(const nlohmann::json& scene)
    {
        // GameObject の型選択は登録一覧、参照の実体化は各 component の ResolveAssets が行う
        // vcam の brain への付け外しは VirtualCamera が OnStart / OnEndPlay で自分で行う
        m_objects.Rebuild(scene, *this, [this](const nlohmann::json& entry) { return ObjectFromJson(entry, m_assets); });
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
        m_sceneRenderer.Render(*brain, *camera, m_skyboxPath);
    }
} // namespace NS::Obj
