#include "Runtime/Object/Scene/Scene.h"

#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/CameraManager.h"
#include "Runtime/Object/Reflection/Archetype.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/UIActor.h"
#include "Runtime/Platform/Clock.h"

#include <limits>
#include <string_view>
#include <vector>

namespace NS::Obj
{
    Scene::Scene() : m_cameraManager(std::make_unique<NS::Obj::CameraManager>())
    {
        m_mainCamera.SetUp({0.0f, 1.0f, 0.0f});
        m_cameraManager->SetCamera(&m_mainCamera);
        // 当たりの調べ役はセンサーの段 (物理の後、仕掛けとゴールの前) で回る
        m_objects.AddTicker(&m_hitSensors, UpdatePhase::Sensors);
    }

    Scene::~Scene() = default;

    void Scene::LoadJson(nlohmann::json scene)
    {
        m_skyboxPath = std::string{SceneJsonSkybox(scene)};
        SetGravityDirection(SceneJsonGravityDirection(scene));
        // 組む前に番号を揃える。未採番のまま組むと id で名指しできない実体ができる
        EnsureUniqueObjectIds(scene);
        RebuildObjectsFrom(scene);
    }

    nlohmann::json Scene::ToJson() const
    {
        nlohmann::json scene = MakeSceneJson();
        SetSceneJsonSkybox(scene, m_skyboxPath);
        SetSceneJsonGravityDirection(scene, m_gravityDirection);
        SetSceneJsonNextObjectId(scene, m_objects.NextObjectId());
        nlohmann::json& objects = SceneJsonObjects(scene);
        for (const Actor* obj : m_objects)
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

    void Scene::SetGravityDirection(const NS::Core::Vector3& direction) noexcept
    {
        m_gravityDirection = NormalizeGravityDirection(direction);
        m_physicsScene.SetGravity(m_gravityDirection * -NS::Phys::k_DefaultGravityY);
    }

    void Scene::SyncPhysics()
    {
        // ギズモで動いた live の当たりを張り直す。object を作り直さないので選択・参照はそのまま保たれる
        m_objects.SyncPhysics(m_physicsScene);
        NotifyTransientsObjectsRebuilt();
    }

    void Scene::NotifyTransientsObjectsRebuilt()
    {
        for (Actor* obj : m_objects)
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
        m_playBaseline = ToJson();
        return m_playBaseline;
    }

    void Scene::WritePlayBaselineField(const Component& comp, std::string_view fieldName)
    {
        const Actor* owner = comp.Owner();
        if (owner == nullptr)
        {
            return;
        }
        const std::size_t index = FindObjectIndexById(m_playBaseline, owner->Id());
        if (index == k_NoObjectIndex)
        {
            return;
        }
        const std::string_view role = owner->PartName(comp);
        if (role.empty())
        {
            return;
        }
        const nlohmann::json fields = SerializePartFields(comp);
        const nlohmann::json::const_iterator value = fields.find(std::string{fieldName});
        if (value == fields.end())
        {
            return;
        }
        ObjectJsonParts(SceneJsonObjects(m_playBaseline)[index])[std::string{role}][std::string{fieldName}] = *value;
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

    Actor* Scene::SpawnTransient(std::unique_ptr<Actor> obj)
    {
        if (!obj)
        {
            return nullptr;
        }

        obj->SetTransient(true);
        obj->AttachScene(this);
        Actor* raw = m_objects.Append(std::move(obj));
        if (raw == nullptr)
        {
            return nullptr;
        }
        StartSpawned(*raw);
        return raw;
    }

    Actor* Scene::SpawnObject(std::unique_ptr<Actor> obj, std::string name)
    {
        if (!obj)
        {
            return nullptr;
        }

        obj->AttachScene(this);
        Actor* raw = m_objects.AppendWithNewId(std::move(obj), std::move(name));
        if (raw == nullptr)
        {
            return nullptr;
        }
        StartSpawned(*raw);
        return raw;
    }

    void Scene::StartSpawned(Actor& obj)
    {
        // データ由来の配置物は ObjectBuilder が引き当てる。後から入る物はここで引き当てる
        // AssetManager が無い間は跳ばす。テストは資産なしでシーンを立てる
        if (m_assets != nullptr)
        {
            obj.ForEachPart([this](std::string_view, Component& part) { part.ResolveAssets(*m_assets); });
        }
        // 開始は引き当ての後。OnStart の中で資産を読む Component が空の参照を掴まない
        obj.OnStart();
        // 後から入る物は他の配置物が既に揃っている
        obj.InitAfterPlacement();
    }

    Actor* Scene::SpawnFromJson(const nlohmann::json& object)
    {
        // 資産の引き当ては開始の直前に StartSpawned がまとめて行う
        std::unique_ptr<Actor> built = ObjectFromJson(object, nullptr);
        if (!built)
        {
            return nullptr;
        }
        built->AttachScene(this);
        Actor* raw = m_objects.InsertFromJson(std::move(built), object, m_objects.ObjectCount());
        if (raw == nullptr)
        {
            return nullptr;
        }
        if (Actor* parent = m_objects.FindByObjectId(ObjectJsonParent(object)); parent != nullptr && parent != raw)
        {
            raw->SetParent(parent);
        }
        StartSpawned(*raw);
        return raw;
    }

    Actor* Scene::ReplaceFromJson(const nlohmann::json& object)
    {
        const std::uint32_t id = ObjectJsonId(object);
        const std::size_t index = m_objects.IndexOfObjectId(id);
        Actor* old = m_objects.FindByObjectId(id);
        if (old == nullptr)
        {
            return SpawnFromJson(object);
        }

        std::unique_ptr<Actor> built = ObjectFromJson(object, nullptr);
        if (!built)
        {
            // 組める component が無い姿は、居ない姿として扱う
            DestroyObject(id);
            return nullptr;
        }

        // 子は古い方の破棄で根に落ちるので、先に控えて新しい方へ付け直す。local の姿はそのまま残る
        std::vector<std::uint32_t> childIds;
        childIds.reserve(old->Children().size());
        for (const Actor* child : old->Children())
        {
            childIds.push_back(child->Id());
        }

        DestroyObject(id);
        built->AttachScene(this);
        Actor* raw = m_objects.InsertFromJson(std::move(built), object, index);
        if (raw == nullptr)
        {
            return nullptr;
        }
        if (Actor* parent = m_objects.FindByObjectId(ObjectJsonParent(object)); parent != nullptr && parent != raw)
        {
            raw->SetParent(parent);
        }
        for (const std::uint32_t childId : childIds)
        {
            if (Actor* child = m_objects.FindByObjectId(childId))
            {
                child->SetParent(raw);
            }
        }
        StartSpawned(*raw);
        return raw;
    }

    Actor* Scene::ApplyFromJson(const nlohmann::json& snapshot)
    {
        Actor* obj = m_objects.FindByObjectId(ObjectJsonId(snapshot));
        if (obj == nullptr)
        {
            return SpawnFromJson(snapshot);
        }
        // 控えは個体の上書きだけを持つ。上書きの無い欄も種類の既定値へ戻すため、全欄の姿へ広げてから比べる
        const nlohmann::json object = ExpandObjectJson(snapshot);
        bool same = ObjectJsonClass(object) == std::string_view{obj->ClassName()};
        std::size_t count = 0;
        obj->ForEachPart([&same, &count, &object](std::string_view role, Component&) {
            ++count;
            if (PartFields(object, role) == nullptr)
            {
                same = false;
            }
        });
        if (!same || count != ObjectJsonParts(object).size())
        {
            return ReplaceFromJson(snapshot);
        }
        const std::string_view name = ObjectJsonName(object);
        if (!name.empty() && obj->Name() != name)
        {
            m_objects.RenameObject(*obj, name);
        }
        obj->SetActive(ObjectJsonActive(object));
        Actor* parent = m_objects.FindByObjectId(ObjectJsonParent(object));
        if (parent == obj)
        {
            parent = nullptr;
        }
        obj->SetParent(parent);
        obj->ForEachPart([this, &object](std::string_view role, Component& part) {
            const nlohmann::json* fields = PartFields(object, role);
            if (fields == nullptr || SerializePartFields(part) == *fields)
            {
                return;
            }
            part.SetEnabled(fields->value("enabled", true));
            (void)ApplyJsonFields(part, *fields);
            if (m_assets != nullptr)
            {
                part.ResolveAssets(*m_assets);
            }
        });
        return obj;
    }

    void Scene::DestroyObject(std::uint32_t objectId)
    {
        m_objects.RemoveByObjectId(objectId);
    }

    void Scene::RebuildObjectsFrom(const nlohmann::json& scene)
    {
        // シーンに 1 つの物 (進行役など) は最初の状態から作り直させる。組み直した配置物の開始が必要な物を作る
        m_sceneObjs.Clear();
        // Actor の型選択は登録一覧、参照の実体化は各 component の ResolveAssets が行う
        // vcam の cameras への付け外しは VirtualCamera が OnStart / OnEndPlay で自分で行う
        m_objects.Rebuild(
            scene, *this, [this](const nlohmann::json& entry) { return ObjectFromJson(entry, m_assets); });
        m_objects.SyncPhysics(m_physicsScene);

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
        m_simulationStepCount += 1;
#if !defined(NS_SHIPPING)
        // 描画 1 回ごとに捨てると、その間に進む固定ステップの回数で映る図形が変わる
        NS::Gfx::DebugDraw::BeginStep();
#endif
        for (UpdatePhase phase : k_UpdatePhases)
        {
            if (phase == UpdatePhase::Physics)
            {
                m_physicsScene.Update(NS::Platform::FrameTimer::FixedDelta());
            }
            // 物理の段に置いた物は、Jolt を 1 歩進めた直後に呼ばれる
            m_objects.ExecutePhase(phase);
            if (phase == UpdatePhase::Camera)
            {
                m_cameraManager->OnTick();
            }
            else if (phase == UpdatePhase::UI)
            {
                const std::vector<UIActor*> uiActors = m_sceneRenderer.UIActors();
                for (UIActor* actor : uiActors)
                {
                    if (std::find(m_sceneRenderer.UIActors().begin(), m_sceneRenderer.UIActors().end(), actor) !=
                            m_sceneRenderer.UIActors().end() &&
                        actor->IsOpen())
                    {
                        actor->Update();
                    }
                }
            }
            else if (phase == UpdatePhase::Effects)
            {
                m_sceneRenderer.UpdateEffects(NS::Platform::FrameTimer::FixedDelta());
            }
        }
        m_objects.RemoveKilledTransients();
    }

    void Scene::OnShutdown()
    {
        // シーンに 1 つの物は配置物と画面の一覧を借りるので先に捨てる
        m_sceneObjs.Clear();
        m_objects.Clear();
        m_cameraManager->SetCamera(nullptr);
    }

    void Scene::OnRender()
    {
        // 更新は終わっているので bounds は 1 フレームに 1 回で足りる。ビューを何枚描いても同じ値
        m_sceneRenderer.SyncRenderBounds();

        NS::Obj::CameraManager* cameras = GetCameraManager();
        CameraComponent* camera = MainCamera();
        if (cameras == nullptr || camera == nullptr)
        {
            return;
        }
        // 世界が実時間で進まない間は、前の固定フレームからの経過の割合に意味が無い
        // 描く度に割合が変わると、同じフレームの絵が揺れる
        float alpha = NS::Platform::FrameTimer::Alpha();
        if (!m_simulationEnabled || m_simulationPaused)
        {
            alpha = 1.0f;
        }
        m_sceneRenderer.Render(*cameras, *camera, m_skyboxPath, alpha);
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

    CameraManager* Scene::GetCameraManager() const noexcept
    {
        return m_cameraManager.get();
    }

    SceneObjHolder* Scene::GetSceneObjHolder() const noexcept
    {
        return &m_sceneObjs;
    }

    NS::Phys::PhysicsScene* Scene::GetPhysicsScene() const noexcept
    {
        // 地形を問う関数のほか、Collider も持ち主の Scene の body を出し入れする時にここから引く
        return const_cast<NS::Phys::PhysicsScene*>(&m_physicsScene);
    }

    NS::Gfx::EffectScene* Scene::GetEffectScene() const noexcept
    {
        return m_sceneRenderer.Effects();
    }

    void Scene::RegisterUIActor(UIActor* actor)
    {
        m_sceneRenderer.RegisterUIActor(actor);
    }

    void Scene::UnregisterUIActor(UIActor* actor) noexcept
    {
        m_sceneRenderer.UnregisterUIActor(actor);
    }

    CameraComponent* Scene::MainCamera() noexcept
    {
        return &m_mainCamera;
    }
} // namespace NS::Obj
