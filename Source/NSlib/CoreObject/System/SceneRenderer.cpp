#include "NSlib/CoreObject/System/SceneRenderer.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/DirectionalLight.h"
#include "NSlib/CoreObject/ClassObjs/OverlayRenderer.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IRenderable.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/CoreObject/System/UIDirector.h"
#include "NSlib/CoreObject/UIActor.h"
#include "NSlib/Gfx/Render/Bloom.h"
#include "NSlib/Gfx/Render/DebugDraw.h"
#include "NSlib/Gfx/Render/EffectScene.h"
#include "NSlib/Gfx/Render/ScreenOverlay.h"
#include "NSlib/Gfx/Render/RenderContext.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/Render/ScreenPasses.h"
#include "NSlib/Gfx/Render/Skybox.h"
#include "NSlib/Windows/Clock.h"
#include "NSlib/Windows/Filesystem.h"

#include <algorithm>
#include <string>
#include <utility>

namespace NS
{
    namespace
    {
        // effectRoot が空ならここを見る
        std::string DefaultEffectRoot()
        {
            const std::string assets = OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), "Assets");
            return OS::FileSystem::Combine(assets, "Effects");
        }
    } // namespace

    SceneRenderer::SceneRenderer() = default;

    SceneRenderer::~SceneRenderer() = default;

    void SceneRenderer::SetDevice(Gfx::D3DDevice* device) noexcept
    {
        m_device = device;
        // 描画先は前の装置で作った物なので、差し替えたら作り直す
        m_viewPasses.clear();
        if (device == nullptr)
        {
            m_effects.reset();
            m_overlay.reset();
            m_skybox.reset();
            m_debugShapes.reset();
            m_shaders = Gfx::BuiltinShaders{};
            return;
        }

        // 描く時は AssetManager を引かない決まりなので、差した時に 1 回だけ引いて描く段へ渡す
        m_shaders = AssetManager::Get().LoadBuiltinShaders();
        m_overlay = std::make_unique<Gfx::ScreenOverlay>(device, m_shaders);
        m_skybox = std::make_unique<Gfx::Skybox>(m_shaders);
        // 資源は今の装置の物なので、装置を差す度に作り直す。出荷では線を描かないので作らない
#if !defined(NS_SHIPPING)
        m_debugShapes = std::make_unique<Gfx::DebugShapePass>(m_shaders);
#endif

        std::string root = m_effectRoot;
        if (root.empty())
        {
            root = DefaultEffectRoot();
        }
        m_effects = std::make_unique<Gfx::EffectScene>(std::move(root));
    }

    void SceneRenderer::SetEffectRoot(std::string root) noexcept
    {
        m_effectRoot = std::move(root);
    }

    void SceneRenderer::OnTick()
    {
        if (m_effects == nullptr)
        {
            return;
        }
        m_effects->Update(OS::FrameTimer::FixedDelta());
    }

    SceneRenderer::RenderEntry* SceneRenderer::FindEntry(const IRenderable* renderable) const noexcept
    {
        NS_ASSERT(Scene, renderable != nullptr, "FindEntry: 描く物が null");
        for (const std::unique_ptr<RenderEntry>& entry : m_renderables)
        {
            if (entry->renderable == renderable)
            {
                return entry.get();
            }
        }
        return nullptr;
    }

    void SceneRenderer::CollectEntry(void* entry, const Gfx::RenderContext& context, std::vector<Gfx::DrawItem>& out)
    {
        const RenderEntry* drawn = static_cast<const RenderEntry*>(entry);
        if (!drawn->enabled)
        {
            return;
        }
        const RenderSource source{.root = drawn->root->InterpolatedWorldMatrix(context.alpha), .model = drawn->model};
        drawn->renderable->Collect(context, source, out);
    }

    RenderSource SceneRenderer::CurrentSource(const RenderEntry& entry) noexcept
    {
        return RenderSource{.root = entry.root->WorldMatrix(), .model = entry.model};
    }

    void SceneRenderer::RegisterRenderable(IRenderable* renderable, const Transform* root, const Model* model)
    {
        NS_ASSERT(Graphics, renderable != nullptr, "SceneRenderer::RegisterRenderable: 描く物が null");
        NS_ASSERT(Graphics, root != nullptr, "SceneRenderer::RegisterRenderable: 持ち主の根が null");
        if (renderable == nullptr || root == nullptr || FindEntry(renderable) != nullptr)
        {
            return;
        }

        std::unique_ptr<RenderEntry> entry = std::make_unique<RenderEntry>();
        entry->renderable = renderable;
        entry->root = root;
        entry->model = model;
        const RenderSource source = CurrentSource(*entry);
        Gfx::RenderProxyDesc desc{};
        desc.bounds = renderable->WorldBounds(source);
        desc.sortCenter = renderable->SortCenter(source);
        desc.sortPriority = renderable->SortPriority();
        desc.transparent = renderable->Bucket() == RenderBucket::Transparent;
        desc.collect = &CollectEntry;
        desc.owner = entry.get();
        entry->handle = m_renderScene.Register(desc);
        m_renderables.push_back(std::move(entry));
    }

    void SceneRenderer::UnregisterRenderable(const IRenderable* renderable) noexcept
    {
        const std::vector<std::unique_ptr<RenderEntry>>::iterator it =
            std::find_if(m_renderables.begin(), m_renderables.end(), [renderable](const std::unique_ptr<RenderEntry>& entry) {
                return entry->renderable == renderable;
            });
        if (it != m_renderables.end())
        {
            m_renderScene.Unregister((*it)->handle);
            m_renderables.erase(it);
        }
        m_bodyMask.erase(std::remove(m_bodyMask.begin(), m_bodyMask.end(), renderable), m_bodyMask.end());
    }

    void SceneRenderer::SetRenderableEnabled(const IRenderable* renderable, bool enabled) noexcept
    {
        NS_ASSERT(Scene, renderable != nullptr, "SetRenderableEnabled: 描く物が null");
        if (RenderEntry* entry = FindEntry(renderable))
        {
            entry->enabled = enabled;
        }
    }

    std::size_t SceneRenderer::EnabledRenderableCount() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(m_renderables.begin(),
                                                      m_renderables.end(),
                                                      [](const std::unique_ptr<RenderEntry>& entry) { return entry->enabled; }));
    }

    std::vector<IRenderable*> SceneRenderer::Renderables() const
    {
        std::vector<IRenderable*> renderables;
        renderables.reserve(m_renderables.size());
        for (const std::unique_ptr<RenderEntry>& entry : m_renderables)
        {
            renderables.push_back(entry->renderable);
        }
        return renderables;
    }

    void SceneRenderer::RegisterOverlay(IOverlay* overlay)
    {
        NS_ASSERT(Graphics, overlay != nullptr, "SceneRenderer::RegisterOverlay: 重ね描きが null");
        if (overlay == nullptr)
        {
            return;
        }
        if (std::any_of(m_overlays.begin(), m_overlays.end(), [overlay](const OverlayEntry& entry) {
                return entry.overlay == overlay;
            }))
        {
            return;
        }

        // 挿入の時点で priority 昇順を保つ。同値は後から来た方が後ろ
        const std::vector<OverlayEntry>::iterator at = std::upper_bound(
            m_overlays.begin(), m_overlays.end(), overlay, [](const IOverlay* a, const OverlayEntry& b) {
                return a->OverlayOrder() < b.overlay->OverlayOrder();
            });
        m_overlays.insert(at, OverlayEntry{.overlay = overlay});
    }

    void SceneRenderer::UnregisterOverlay(const IOverlay* overlay) noexcept
    {
        std::erase_if(m_overlays, [overlay](const OverlayEntry& entry) { return entry.overlay == overlay; });
    }

    void SceneRenderer::SetOverlayEnabled(const IOverlay* overlay, bool enabled) noexcept
    {
        NS_ASSERT(Scene, overlay != nullptr, "SetOverlayEnabled: 重ね描きが null");
        for (OverlayEntry& entry : m_overlays)
        {
            if (entry.overlay == overlay)
            {
                entry.enabled = enabled;
            }
        }
    }

    std::size_t SceneRenderer::EnabledOverlayCount() const noexcept
    {
        return static_cast<std::size_t>(
            std::count_if(m_overlays.begin(), m_overlays.end(), [](const OverlayEntry& entry) { return entry.enabled; }));
    }

    void SceneRenderer::RegisterLight(DirectionalLight* light, const Transform* root)
    {
        NS_ASSERT(Graphics, light != nullptr, "SceneRenderer::RegisterLight: 平行光が null");
        NS_ASSERT(Graphics, root != nullptr, "SceneRenderer::RegisterLight: 持ち主の根が null");
        if (light == nullptr || root == nullptr)
        {
            return;
        }
        // 二重に積むと UnregisterLight が片方しか消さず、外したはずの光が残る
        if (std::any_of(
                m_lights.begin(), m_lights.end(), [light](const LightEntry& entry) { return entry.light == light; }))
        {
            return;
        }
        m_lights.push_back(LightEntry{.light = light, .root = root});
    }

    void SceneRenderer::UnregisterLight(const DirectionalLight* light) noexcept
    {
        std::erase_if(m_lights, [light](const LightEntry& entry) { return entry.light == light; });
    }

    void SceneRenderer::SetLightEnabled(const DirectionalLight* light, bool enabled) noexcept
    {
        NS_ASSERT(Scene, light != nullptr, "SetLightEnabled: 平行光が null");
        for (LightEntry& entry : m_lights)
        {
            if (entry.light == light)
            {
                entry.enabled = enabled;
            }
        }
    }

    std::size_t SceneRenderer::EnabledLightCount() const noexcept
    {
        return static_cast<std::size_t>(
            std::count_if(m_lights.begin(), m_lights.end(), [](const LightEntry& entry) { return entry.enabled; }));
    }

    void SceneRenderer::SyncRenderBounds()
    {
        // proxy 側の bounds はコピーなので、描画前に登録元の現在値へ揃える。無効の組は描かないので写さない
        for (const std::unique_ptr<RenderEntry>& entry : m_renderables)
        {
            if (!entry->enabled)
            {
                continue;
            }
            const IRenderable* r = entry->renderable;
            const RenderSource source = CurrentSource(*entry);
            m_renderScene.Update(entry->handle,
                                 r->WorldBounds(source),
                                 r->SortCenter(source),
                                 r->SortPriority(),
                                 r->Bucket() == RenderBucket::Transparent);
        }
    }

    void SceneRenderer::DrawOpaque(const Gfx::RenderContext& context)
    {
        m_renderScene.DrawBucket(context, false);
    }

    void SceneRenderer::DrawTransparent(const Gfx::RenderContext& context)
    {
        m_renderScene.DrawBucket(context, true);
    }

    void SceneRenderer::DrawOverlays(const Gfx::RenderContext& context,
                                     UIDirector& ui,
                                     const Gfx::ScreenFade& fade)
    {
        for (const OverlayEntry& entry : m_overlays)
        {
            if (entry.enabled)
            {
                entry.overlay->OnRenderOverlay(context);
            }
        }
        // 画面に出す物は世界の上の重ね描きよりさらに上
        ui.Draw(context);
        // 暗転は UIActor の描く順に依らず、全ての UI と光を覆う
        if (context.overlay != nullptr)
        {
            fade.Render(*context.overlay);
        }
    }

    Gfx::RenderSettings SceneRenderer::ResolveSceneSettings(const Gfx::RenderSettings& projectDefaults)
    {
        Gfx::RenderSettings resolved = projectDefaults;
        for (const LightEntry& entry : m_lights)
        {
            if (!entry.enabled)
            {
                continue;
            }
            const DirectionalLight* light = entry.light;
            const Vector3 direction = light->Direction(entry.root->WorldMatrix());
            // 0 ベクトルはシェーダ側の正規化で拡散光が消えるため、上書きせず既定の lightDir に残す
            if (direction.LengthSquared() > 1e-6f)
            {
                resolved.lightDir = direction;
            }
            else if (!m_warnedZeroLightDirection)
            {
                NS_LOG_WARN(Graphics, "SceneRenderer: 平行光の Direction が 0 のため既定 lightDir で描画する");
                m_warnedZeroLightDirection = true;
            }
            // 多灯合成を持たないので、後から登録した有効な 1 本が前の値を上書きする
            resolved.lightColor = light->Color().ToVector3();
            resolved.ambientColor = light->Ambient().ToVector3();
            resolved.groundColor = light->Ground().ToVector3();
            resolved.exposure = light->Exposure();
        }
        return resolved;
    }

    void SceneRenderer::Render(SceneCamera& camera,
                               UIDirector& ui,
                               const Gfx::ScreenFade& fade,
                               std::string_view skyboxPath,
                               float alpha)
    {
        if (m_device == nullptr)
        {
            return;
        }

        // ビュー列が空なら現描画先へ実カメラの視点で 1 回だけ描く。描画先は BeginFrame が bind 済み
        if (m_sceneViews.empty())
        {
            RenderViewWithOverlays(camera, ui, fade, skyboxPath, SceneView{}, alpha, PassesForView(0));
            return;
        }

        for (std::size_t i = 0; i < m_sceneViews.size(); ++i)
        {
            const SceneView& view = m_sceneViews[i];
            m_device->BeginSceneView(view.target);
            RenderViewWithOverlays(camera, ui, fade, skyboxPath, view, alpha, PassesForView(i));
        }
    }

    const Gfx::D3DTexture* SceneRenderer::BodyMaskShown(std::size_t viewIndex) const noexcept
    {
        if (viewIndex >= m_viewPasses.size())
        {
            return nullptr;
        }
        return m_viewPasses[viewIndex].screen->BodyMask();
    }

    void SceneRenderer::DrawBodyMask(const Gfx::RenderContext& context, Gfx::ScreenPasses& screen)
    {
        if (m_bodyMask.empty() || !screen.BeginBodyMask(*m_device))
        {
            screen.DropBodyMask();
            return;
        }
        m_bodyMaskItems.clear();
        for (const IRenderable* renderable : m_bodyMask)
        {
            if (RenderEntry* entry = FindEntry(renderable))
            {
                CollectEntry(entry, context, m_bodyMaskItems);
            }
        }
        for (const Gfx::DrawItem& item : m_bodyMaskItems)
        {
            screen.DrawBodyMaskItem(*m_device, item);
        }
        screen.EndBodyMask(*m_device);
    }

    SceneRenderer::ViewPasses& SceneRenderer::PassesForView(std::size_t index)
    {
        while (m_viewPasses.size() <= index)
        {
            ViewPasses passes;
            passes.bloom = std::make_unique<Gfx::Bloom>(Gfx::BloomDesc{}, m_shaders);
            passes.screen = std::make_unique<Gfx::ScreenPasses>(m_shaders);
            m_viewPasses.push_back(std::move(passes));
        }
        return m_viewPasses[index];
    }

    void SceneRenderer::RenderViewWithOverlays(SceneCamera& camera,
                                               UIDirector& ui,
                                               const Gfx::ScreenFade& fade,
                                               std::string_view skyboxPath,
                                               const SceneView& view,
                                               float alpha,
                                               ViewPasses& passes)
    {
        const Gfx::RenderContext ctx = RenderWorld(camera, skyboxPath, view.viewPose, alpha, passes);

#if !defined(NS_SHIPPING)
        m_debugShapes->Draw(*ctx.device, Gfx::DebugDraw::StepShapes(), ctx.viewProjection);

        // ビューの図形は、このビューの行列で積んで描いたらすぐ捨てる。次のビューや次のフレームへ持ち越さない
        if (view.drawShapes)
        {
            m_viewShapes.Clear();
            view.drawShapes(m_viewShapes, ctx.viewProjection);
            m_debugShapes->Draw(*ctx.device, m_viewShapes, ctx.viewProjection);
            m_viewShapes.Clear();
        }
#endif

        DrawOverlays(ctx, ui, fade);
    }

    namespace
    {
        // 歪みの輪を描画先の画素へ直す。中心がカメラの後ろか、描画先が無い時は歪めない
        [[nodiscard]] Gfx::ScreenRing ToScreenRing(const Gfx::DistortionRing& ring,
                                                       const Matrix& viewProjection,
                                                       Size2D size) noexcept
        {
            if (!(ring.push > 0.0f) || size.width <= 0 || size.height <= 0)
            {
                return Gfx::ScreenRing{};
            }
            const float width = static_cast<float>(size.width);
            const float height = static_cast<float>(size.height);
            Vector2 centerPixel{};
            float centerW = 0.0f;
            if (!Gfx::TryProjectToPixels(viewProjection, ring.center, width, height, centerPixel, centerW))
            {
                return Gfx::ScreenRing{};
            }
            return Gfx::ScreenRing{.centerPixel = centerPixel,
                                       .radiusPixels = ring.radius * height,
                                       .pushPixels = ring.push * height,
                                       .halfWidthPixels = ring.halfWidth * height};
        }
    } // namespace

    Gfx::RenderContext SceneRenderer::RenderWorld(SceneCamera& camera,
                                                      std::string_view skyboxPath,
                                                      const std::optional<CameraPose>& viewOverride,
                                                      float alpha,
                                                      ViewPasses& passes)
    {
        // 装置の現在サイズから毎回取り直し、リサイズとビュー切替に追従する
        camera.SetAspectRatioFromDevice(*m_device);

        Gfx::RenderContext ctx{};
        ctx.device = m_device;
        ctx.overlay = m_overlay.get();
        ctx.alpha = alpha;

        // 上書き視点は実カメラを経由せず、その場で組む。実カメラの中身はゲーム視点のまま残す
        CameraData overrideCamera{};
        const CameraData* viewCamera = &camera.Camera();
        if (viewOverride.has_value())
        {
            overrideCamera.SetPosition(viewOverride->position);
            overrideCamera.SetTarget(viewOverride->target);
            overrideCamera.SetUp(viewOverride->up);
            overrideCamera.SetFovY(viewOverride->fovY);
            overrideCamera.SetNearPlane(viewOverride->nearPlane);
            overrideCamera.SetFarPlane(viewOverride->farPlane);

            // aspect は関数の冒頭で device から取り直した実カメラの物を写す
            overrideCamera.SetAspectRatio(camera.Camera().AspectRatio());
            viewCamera = &overrideCamera;
        }
        ctx.viewProjection = viewCamera->ViewProjection();
        ctx.cameraPosition = viewCamera->Position();
        ctx.resolvedSettings = ResolveSceneSettings(Gfx::RenderSettings{});
        ctx.groundWave = m_groundWave;
        ctx.distortionRing = m_distortionRing;

        // 物の絵は S 字で 1 以下に書くので、にじむのは 1 を超えて書いたエフェクトだけ
        // 深度は今の描画先の物を使うので、後で描くデバッグの線も世界の深度で隠れる
        passes.bloom->BeginWorld(*m_device, m_device->ClearColor());
        DrawOpaque(ctx);
        m_skybox->Draw(*m_device, *viewCamera, skyboxPath);
        DrawTransparent(ctx);
        // ワールド空間の半透明物。半透明の後、デバッグ描画の前
        if (m_effects != nullptr)
        {
            m_effects->Draw(*viewCamera);
        }
        // 重ね描きとデバッグの線はにじませも歪ませもしないので、この後に今の描画先へ描く
        passes.bloom->EndWorld(*m_device);
        DrawBodyMask(ctx, *passes.screen);
        passes.screen->Distort(*m_device, ToScreenRing(ctx.distortionRing, ctx.viewProjection, m_device->Size()));
        return ctx;
    }
} // namespace NS
