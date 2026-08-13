#include "Runtime/Object/Scene/Scene.h"

#include "Runtime/Core/Clock.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Object/Components/CameraBrainComponent.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/DirectionalLightComponent.h"
#include "Runtime/Object/Components/OverlayRendererComponent.h"
#include "Runtime/Object/IRenderable.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"

namespace NS::Object
{
    namespace
    {
        // RenderScene の proxy を IRenderable::Collect へつなぐ。owner は登録元の IRenderable
        void CollectRenderable(void* owner,
                               const NS::Graphics::RenderContext& context,
                               std::vector<NS::Graphics::DrawItem>& out)
        {
            static_cast<IRenderable*>(owner)->Collect(context, out);
        }
    } // namespace

    Scene::Scene()
    {
        // 描くには実カメラが 1 個要る。 配置物ではないがシーンには必ず居るので、 ここで world へ入れる
        // 保存・凍結・編集 UI に出ない一時オブジェクトで、 データからの組み直しも跨いで残る
        // 描画 component は積まない。 RegisterRenderable は virtual で、 基底コンストラクタからは派生へ落ちない
        auto host = std::make_unique<GameObject>();
        CameraComponent* camera = host->AddComponent<CameraComponent>();
        camera->SetUp({0.0f, 1.0f, 0.0f});
        m_brain = host->AddComponent<CameraBrainComponent>();
        SpawnTransient(std::move(host));
    }

    Scene::~Scene() = default;

    void Scene::LoadFromData(SceneData&& data)
    {
        m_environment = data.environment;
        // 組む前に番号を揃える。未採番のまま組むと id で名指しできない実体ができる
        EnsureUniqueObjectIds(data);
        RebuildWorldFrom(data);
    }

    void Scene::SyncPhysics()
    {
        // ギズモで動いた live の当たりを張り直す。 object を作り直さないので選択・参照はそのまま保たれる
        m_world.RebuildPhysics(Physics());
        OnWorldChanged();
        NotifyTransientsWorldChanged();
    }

    void Scene::NotifyTransientsWorldChanged()
    {
        for (GameObject* obj : m_world)
        {
            if (obj->IsTransient())
                obj->OnWorldChanged();
        }
    }

    const SceneData& Scene::BeginPlayBaseline()
    {
        // プレイ規則の判定と編集復帰の姿はこの凍結を読む。 プレイ中の変化は凍結に映らず、編集へ持ち込まれない
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
            return nullptr;
        obj->SetTransient(true);
        obj->AttachScene(this);
        GameObject* raw = m_world.Append(std::move(obj));
        // データ由来の配置物は Rebuild が開始まで面倒を見る。 後から入る一時オブジェクトはここで開始する
        if (raw != nullptr)
            raw->OnStart();
        return raw;
    }

    SceneData Scene::CaptureLiveToSceneData() const
    {
        // 一時オブジェクトを除く全 object を、 全 component 値まで忠実に写す
        SceneData data{};
        data.environment = m_environment;
        data.nextObjectId = m_world.NextObjectId();

        data.objects.reserve(m_world.ObjectCount());
        for (const GameObject* obj : m_world)
        {
            if (obj->IsTransient())
                continue;
            ObjectData od = CaptureObjectData(*obj);
            od.objectId = obj->Id();
            data.objects.push_back(std::move(od));
        }
        return data;
    }

    void Scene::DestroyObject(std::uint32_t objectId)
    {
        m_world.RemoveByObjectId(objectId, Physics());
    }

    void Scene::RebuildWorldFrom(const SceneData& data)
    {
        // GameObject の型選択は登録一覧、 参照の実体化は各 component の ResolveAssets が行う
        // vcam の brain への付け外しは VirtualCameraComponent が OnStart / OnEndPlay で自分で行う
        m_world.Rebuild(
            data, *this, Physics(), [this](const ObjectData& entry) { return BuildSceneObject(entry, m_assets); });

        OnWorldChanged();
        NotifyTransientsWorldChanged();
    }

    void Scene::OnUpdate()
    {
        // 補間描画用。 全表示オブジェクトの状態をスナップショットする
        for (GameObject* obj : m_world)
        {
            obj->Root().Snapshot();
        }

        // 世界の駆動。 読み込んだら回り続けるのが既定で、 編集モードのエディタだけが止める
        // 時間停止中は上の snapshot だけが残り、 previous == current で補間が凍る
        if (!m_simulationEnabled)
            return;
        if (m_simulationPaused)
        {
            if (m_simulationStepFrames <= 0)
                return;
            m_simulationStepFrames -= 1;
        }
        m_world.UpdateAllObjects();
    }

    void Scene::OnShutdown()
    {
        m_world.Clear();
        // host も world と一緒に消えた。 控えを残すと破棄済みを指し続ける
        m_brain = nullptr;
    }

    std::optional<NS::Graphics::RenderContext> Scene::RenderWorld(NS::Graphics::Renderer& renderer,
                                                                  const std::optional<CameraPose>& viewOverride)
    {
        // 描画コンテキストの準備
        CameraBrainComponent* brain = CameraBrain();
        CameraComponent* mainCamera = MainCamera();
        if (brain == nullptr || mainCamera == nullptr)
        {
            return std::nullopt;
        }

        // アスペクト比をレンダラーの現在サイズへ同期する。 リサイズ追従もここで済む
        mainCamera->SetAspectRatioFromRenderer(renderer);

        NS::Graphics::RenderContext ctx{};
        ctx.renderer = &renderer;
        ctx.alpha = NS::Core::FrameTimer::Alpha();

        brain->Evaluate(ctx.alpha);

        // 上書き視点は実カメラを経由せず、その場で行列を組む。実カメラの中身はゲーム視点のまま残す
        NS::Graphics::Camera overrideCamera{};
        const NS::Graphics::Camera* skyCamera = &mainCamera->Camera();
        if (viewOverride.has_value())
        {
            overrideCamera.SetPosition(viewOverride->position);
            overrideCamera.SetTarget(viewOverride->target);
            overrideCamera.SetUp(viewOverride->up);
            overrideCamera.SetFovY(viewOverride->fovY);
            overrideCamera.SetNearPlane(viewOverride->nearPlane);
            overrideCamera.SetFarPlane(viewOverride->farPlane);

            // aspect は実カメラと同じ規則で renderer から取る。 幅か高さが 0 以下なら 16:9
            const NS::Core::Size2D size = renderer.Size();
            const float aspect = [&]() -> float {
                if (size.width <= 0 || size.height <= 0)
                {
                    return 16.0f / 9.0f;
                }
                return NS::Core::AspectRatio(size);
            }();
            overrideCamera.SetAspectRatio(aspect);

            ctx.viewProjection = overrideCamera.ViewProjection();
            ctx.cameraPosition = viewOverride->position;
            skyCamera = &overrideCamera;
        }
        else
        {
            ctx.viewProjection = brain->ViewProjection();
            ctx.cameraPosition = mainCamera->Camera().View().Invert().Translation();
        }
        ctx.resolvedSettings = ResolveSceneSettings(renderer.Settings());

        // レンダリングパス
        DrawOpaque(ctx);
        renderer.DrawSky(*skyCamera, m_environment.skyboxCubemapPath);
        DrawTransparent(ctx);
        return ctx;
    }

    NS::Graphics::RenderSettings Scene::ResolveSceneSettings(const NS::Graphics::RenderSettings& projectDefaults)
    {
        NS::Graphics::RenderSettings resolved = projectDefaults;
        // 照明は配置された平行光から取る。無ければ project 既定値がそのまま残る
        // 複数置かれた場合は多灯合成せず、走査順で最後の有効な 1 本が勝つ
        m_world.ForEachComponent<DirectionalLightComponent>([&resolved, this](DirectionalLightComponent& light) {
            if (!light.IsActive())
                return;
            if (light.Direction().LengthSquared() > 1e-6f)
            {
                resolved.lightDir = light.Direction();
            }
            else if (!m_warnedZeroLightDirection)
            {
                // zero ベクトルは normalize で拡散光が無言で消えるため上書きせず既定 lightDir に落とす
                NS_LOG_WARN(Graphics, "Scene: 平行光の Direction が zero のため既定 lightDir で描画する");
                m_warnedZeroLightDirection = true;
            }
            resolved.lightColor = light.Color();
            resolved.ambientColor = light.Ambient();
            resolved.groundColor = light.Ground();
            resolved.exposure = light.Exposure();
        });
        return resolved;
    }

    void Scene::OnRender()
    {
        // 更新は終わっているので bounds は 1 フレームに 1 回で足りる。ビューを何枚描いても同じ値
        SyncRenderBounds();
        // renderer と camera は Game 層しか知らないため、コンテキスト構築は OnRenderScene に任せる
        OnRenderScene();
    }

    void Scene::RegisterRenderable(IRenderable* renderable)
    {
        if (renderable == nullptr)
            return;
        // 二重登録を防ぐ。Component 側で OnStart が誤って 2 回呼ばれても二重描画にならない
        for (const RenderEntry& entry : m_renderables)
            if (entry.renderable == renderable)
                return;

        NS::Graphics::RenderProxyDesc desc{};
        desc.bounds = renderable->WorldBounds();
        desc.sortCenter = renderable->SortCenter();
        desc.sortPriority = renderable->SortPriority();
        desc.transparent = renderable->Bucket() == RenderBucket::Transparent;
        desc.collect = &CollectRenderable;
        desc.owner = renderable;
        m_renderables.push_back({renderable, m_renderScene.Register(desc)});
    }

    void Scene::UnregisterRenderable(IRenderable* renderable)
    {
        if (renderable == nullptr)
            return;
        for (auto it = m_renderables.begin(); it != m_renderables.end(); ++it)
        {
            if (it->renderable != renderable)
                continue;
            m_renderScene.Unregister(it->handle);
            m_renderables.erase(it);
            return;
        }
    }

    void Scene::SyncRenderBounds()
    {
        // proxy 側の bounds はコピーなので、描画前に登録元の現在値へ揃える
        for (const RenderEntry& entry : m_renderables)
        {
            IRenderable* r = entry.renderable;
            if (r == nullptr)
                continue;
            m_renderScene.Update(entry.handle,
                                 r->WorldBounds(),
                                 r->SortCenter(),
                                 r->SortPriority(),
                                 r->Bucket() == RenderBucket::Transparent);
        }
    }

    void Scene::DrawOpaque(const NS::Graphics::RenderContext& context)
    {
        m_renderScene.DrawBucket(context, false);
    }

    void Scene::DrawTransparent(const NS::Graphics::RenderContext& context)
    {
        m_renderScene.DrawBucket(context, true);
    }

    CameraBrainComponent* Scene::CameraBrain() noexcept
    {
        return m_brain;
    }

    CameraComponent* Scene::MainCamera() noexcept
    {
        if (m_brain == nullptr)
            return nullptr;
        return m_brain->Camera();
    }

    void Scene::OnRenderScene()
    {
        if (m_renderer == nullptr)
        {
            return;
        }

        // ビュー列が空なら現描画先へ Brain 視点で 1 回だけ描く。 描画先は BeginFrame が bind 済み
        if (m_sceneViews.empty())
        {
            RenderViewWithOverlays(std::nullopt);
            return;
        }

        // 可視ビューの数だけ、 各ビューの描画先へ切り替えてその視点で描く
        for (const SceneView& view : m_sceneViews)
        {
            m_renderer->BeginSceneView(view.target);
            RenderViewWithOverlays(view.viewPose);
        }
    }

    void Scene::RenderViewWithOverlays(const std::optional<CameraPose>& viewOverride)
    {
        const std::optional<NS::Graphics::RenderContext> ctx = RenderWorld(*m_renderer, viewOverride);
        if (!ctx)
        {
            return;
        }

#if !defined(NS_SHIPPING)
        // 溜まったデバッグ線をここで一括で描く
        NS::Graphics::DebugDraw::Flush(*ctx->renderer, ctx->viewProjection);
#endif

        // 重ね描きを持つ component を最前面へ重ねる。 演出の中身はゲーム側の component が持つ
        // 並びは配置物の順、 その中は component を積んだ順。 帯の priority は見ない
        for (GameObject* obj : m_world)
        {
            for (Component* comp : obj->Components())
            {
                auto* overlay = ComponentCast<OverlayRendererComponent>(comp);
                // active を切った component は描かない。 更新・ 当たりと同じ問いで揃える
                if (overlay != nullptr && overlay->IsActive())
                    overlay->OnRenderOverlay(*ctx);
            }
        }
    }
} // namespace NS::Object
