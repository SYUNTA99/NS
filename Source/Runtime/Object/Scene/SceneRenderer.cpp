#include "Runtime/Object/Scene/SceneRenderer.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Graphics/EffectScene.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Object/Components/CameraBrain.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/DirectionalLight.h"
#include "Runtime/Object/Components/OverlayRenderer.h"
#include "Runtime/Object/IRenderable.h"
#include "Runtime/Platform/Clock.h"
#include "Runtime/Platform/Filesystem.h"

#include <algorithm>
#include <string>
#include <utility>

namespace NS::Obj
{
    namespace
    {
        // RenderProxyList の proxy を IRenderable::Collect へつなぐ。owner は登録元の IRenderable
        void CollectRenderable(void* owner, const NS::Gfx::RenderContext& context, std::vector<NS::Gfx::DrawItem>& out)
        {
            static_cast<IRenderable*>(owner)->Collect(context, out);
        }

        // effectRoot が空ならここを見る
        std::string DefaultEffectRoot()
        {
            const std::string assets =
                NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::ContentRoot(), "Assets");
            return NS::Platform::FileSystem::Combine(assets, "Effects");
        }
    } // namespace

    SceneRenderer::SceneRenderer() = default;

    SceneRenderer::~SceneRenderer() = default;

    void SceneRenderer::SetRenderer(NS::Gfx::Renderer* renderer) noexcept
    {
        m_renderer = renderer;
        if (renderer == nullptr)
        {
            m_effects.reset();
            return;
        }

        std::string root = m_effectRoot;
        if (root.empty())
        {
            root = DefaultEffectRoot();
        }
        m_effects = std::make_unique<NS::Gfx::EffectScene>(std::move(root));
    }

    void SceneRenderer::SetEffectRoot(std::string root) noexcept
    {
        m_effectRoot = std::move(root);
    }

    void SceneRenderer::UpdateEffects(float deltaSeconds) noexcept
    {
        if (m_effects == nullptr)
        {
            return;
        }
        m_effects->Update(deltaSeconds);
    }

    void SceneRenderer::RegisterRenderable(IRenderable* renderable)
    {
        if (renderable == nullptr)
        {
            return;
        }
        for (const RenderEntry& entry : m_renderables)
        {
            if (entry.renderable == renderable)
            {
                return;
            }
        }

        NS::Gfx::RenderProxyDesc desc{};
        desc.bounds = renderable->WorldBounds();
        desc.sortCenter = renderable->SortCenter();
        desc.sortPriority = renderable->SortPriority();
        desc.transparent = renderable->Bucket() == RenderBucket::Transparent;
        desc.collect = &CollectRenderable;
        desc.owner = renderable;
        m_renderables.push_back({renderable, m_renderScene.Register(desc)});
    }

    void SceneRenderer::UnregisterRenderable(IRenderable* renderable)
    {
        if (renderable == nullptr)
        {
            return;
        }
        for (auto it = m_renderables.begin(); it != m_renderables.end(); ++it)
        {
            if (it->renderable != renderable)
            {
                continue;
            }
            m_renderScene.Unregister(it->handle);
            m_renderables.erase(it);
            return;
        }
    }

    void SceneRenderer::RegisterOverlay(OverlayRenderer* overlay)
    {
        if (overlay == nullptr)
        {
            return;
        }
        for (const OverlayRenderer* entry : m_overlays)
        {
            if (entry == overlay)
            {
                return;
            }
        }

        // 挿入の時点で priority 昇順を保つ。同値は後から来た方が後ろ
        const auto at = std::upper_bound(
            m_overlays.begin(), m_overlays.end(), overlay, [](const OverlayRenderer* a, const OverlayRenderer* b) {
                return a->Priority() < b->Priority();
            });
        m_overlays.insert(at, overlay);
    }

    void SceneRenderer::UnregisterOverlay(OverlayRenderer* overlay)
    {
        if (overlay == nullptr)
        {
            return;
        }
        for (auto it = m_overlays.begin(); it != m_overlays.end(); ++it)
        {
            if (*it != overlay)
            {
                continue;
            }
            m_overlays.erase(it);
            return;
        }
    }

    void SceneRenderer::RegisterLight(DirectionalLight* light)
    {
        if (light == nullptr)
        {
            return;
        }
        // 二重に積むと UnregisterLight が片方しか消さず、外したはずの光が残る
        for (const DirectionalLight* entry : m_lights)
        {
            if (entry == light)
            {
                return;
            }
        }
        m_lights.push_back(light);
    }

    void SceneRenderer::UnregisterLight(DirectionalLight* light)
    {
        if (light == nullptr)
        {
            return;
        }
        for (auto it = m_lights.begin(); it != m_lights.end(); ++it)
        {
            if (*it != light)
            {
                continue;
            }
            m_lights.erase(it);
            return;
        }
    }

    void SceneRenderer::SyncRenderBounds()
    {
        // proxy 側の bounds はコピーなので、描画前に登録元の現在値へ揃える
        for (const RenderEntry& entry : m_renderables)
        {
            IRenderable* r = entry.renderable;
            if (r == nullptr)
            {
                continue;
            }
            m_renderScene.Update(entry.handle,
                                 r->WorldBounds(),
                                 r->SortCenter(),
                                 r->SortPriority(),
                                 r->Bucket() == RenderBucket::Transparent);
        }
    }

    void SceneRenderer::DrawOpaque(const NS::Gfx::RenderContext& context)
    {
        m_renderScene.DrawBucket(context, false);
    }

    void SceneRenderer::DrawTransparent(const NS::Gfx::RenderContext& context)
    {
        m_renderScene.DrawBucket(context, true);
    }

    void SceneRenderer::DrawOverlays(const NS::Gfx::RenderContext& context)
    {
        // 更新・当たりと同じ IsActive で切る。自分の値だけ見ると親を寝かせても描き続ける
        for (OverlayRenderer* overlay : m_overlays)
        {
            if (overlay != nullptr && overlay->IsActive())
            {
                overlay->OnRenderOverlay(context);
            }
        }
    }

    NS::Gfx::RenderSettings SceneRenderer::ResolveSceneSettings(const NS::Gfx::RenderSettings& projectDefaults)
    {
        NS::Gfx::RenderSettings resolved = projectDefaults;
        for (DirectionalLight* light : m_lights)
        {
            if (!light->IsActive())
            {
                continue;
            }
            // 0 ベクトルはシェーダ側の正規化で拡散光が消えるため、上書きせず既定の lightDir に残す
            if (light->Direction().LengthSquared() > 1e-6f)
            {
                resolved.lightDir = light->Direction();
            }
            else if (!m_warnedZeroLightDirection)
            {
                NS_LOG_WARN(Graphics, "SceneRenderer: 平行光の Direction が 0 のため既定 lightDir で描画する");
                m_warnedZeroLightDirection = true;
            }
            // 多灯合成を持たないので、後から登録した有効な 1 本が前の値を上書きする
            resolved.lightColor = light->Color();
            resolved.ambientColor = light->Ambient();
            resolved.groundColor = light->Ground();
            resolved.exposure = light->Exposure();
        }
        return resolved;
    }

    void SceneRenderer::Render(CameraBrain& brain, CameraComponent& camera, const SceneEnvironment& environment)
    {
        if (m_renderer == nullptr)
        {
            return;
        }

        // ビュー列が空なら現描画先へ Brain の視点で 1 回だけ描く。描画先は BeginFrame が bind 済み
        if (m_sceneViews.empty())
        {
            RenderViewWithOverlays(brain, camera, environment, std::nullopt);
            return;
        }

        for (const SceneView& view : m_sceneViews)
        {
            m_renderer->BeginSceneView(view.target);
            RenderViewWithOverlays(brain, camera, environment, view.viewPose);
        }
    }

    void SceneRenderer::RenderViewWithOverlays(CameraBrain& brain,
                                               CameraComponent& camera,
                                               const SceneEnvironment& environment,
                                               const std::optional<CameraPose>& viewOverride)
    {
        const NS::Gfx::RenderContext ctx = RenderWorld(brain, camera, environment, viewOverride);

#if !defined(NS_SHIPPING)
        NS::Gfx::DebugDraw::Flush(*ctx.renderer, ctx.viewProjection);
#endif

        DrawOverlays(ctx);
    }

    NS::Gfx::RenderContext SceneRenderer::RenderWorld(CameraBrain& brain,
                                                      CameraComponent& camera,
                                                      const SceneEnvironment& environment,
                                                      const std::optional<CameraPose>& viewOverride)
    {
        // レンダラーの現在サイズから毎回取り直し、リサイズとビュー切替に追従する
        camera.SetAspectRatioFromRenderer(*m_renderer);

        NS::Gfx::RenderContext ctx{};
        ctx.renderer = m_renderer;
        ctx.alpha = NS::Platform::FrameTimer::Alpha();

        brain.Evaluate(ctx.alpha);

        // 上書き視点は実カメラを経由せず、その場で行列を組む。実カメラの中身はゲーム視点のまま残す
        NS::Core::CameraData overrideCamera{};
        const NS::Core::CameraData* viewCamera = &camera.Camera();
        if (viewOverride.has_value())
        {
            overrideCamera.SetPosition(viewOverride->position);
            overrideCamera.SetTarget(viewOverride->target);
            overrideCamera.SetUp(viewOverride->up);
            overrideCamera.SetFovY(viewOverride->fovY);
            overrideCamera.SetNearPlane(viewOverride->nearPlane);
            overrideCamera.SetFarPlane(viewOverride->farPlane);

            // aspect は実カメラと同じ規則で renderer から取る。幅か高さが 0 以下なら 16:9
            const NS::Core::Size2D size = m_renderer->Size();
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
            viewCamera = &overrideCamera;
        }
        else
        {
            ctx.viewProjection = brain.ViewProjection();
            ctx.cameraPosition = camera.Position();
        }
        ctx.resolvedSettings = ResolveSceneSettings(m_renderer->Settings());

        DrawOpaque(ctx);
        m_renderer->DrawSky(*viewCamera, environment.skyboxCubemapPath);
        DrawTransparent(ctx);
        // ワールド空間の半透明物。半透明の後、デバッグ描画の前
        if (m_effects != nullptr)
        {
            m_effects->Draw(*viewCamera);
        }
        return ctx;
    }
} // namespace NS::Obj
