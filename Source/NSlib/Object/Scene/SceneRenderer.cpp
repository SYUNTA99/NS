#include "NSlib/Object/Scene/SceneRenderer.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Graphics/Bloom.h"
#include "NSlib/Graphics/DebugDraw.h"
#include "NSlib/Graphics/EffectScene.h"
#include "NSlib/Graphics/RenderContext.h"
#include "NSlib/Graphics/Renderer.h"
#include "NSlib/Object/SubObjects/CameraManager.h"
#include "NSlib/Object/SubObjects/DirectionalLight.h"
#include "NSlib/Object/SubObjects/OverlayRenderer.h"
#include "NSlib/Object/IRenderable.h"
#include "NSlib/Object/Scene/SceneCamera.h"
#include "NSlib/Object/UIActor.h"
#include "NSlib/Windows/Clock.h"
#include "NSlib/Windows/Filesystem.h"

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
            const std::string assets = NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets");
            return NS::OS::FileSystem::Combine(assets, "Effects");
        }
    } // namespace

    SceneRenderer::SceneRenderer() = default;

    SceneRenderer::~SceneRenderer() = default;

    void SceneRenderer::SetRenderer(NS::Gfx::Renderer* renderer) noexcept
    {
        m_renderer = renderer;
        // 描画先は前のレンダラーの device で作った物なので、差し替えたら作り直す
        m_blooms.clear();
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

    void SceneRenderer::OnTick()
    {
        if (m_effects == nullptr)
        {
            return;
        }
        m_effects->Update(NS::OS::FrameTimer::FixedDelta());
    }

    void SceneRenderer::RegisterRenderable(IRenderable* renderable)
    {
        if (renderable == nullptr)
        {
            return;
        }
        const auto isSame = [renderable](const RenderEntry& entry) { return entry.renderable == renderable; };
        if (std::any_of(m_renderables.begin(), m_renderables.end(), isSame))
        {
            return;
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
        const std::vector<RenderEntry>::iterator it =
            std::find_if(m_renderables.begin(), m_renderables.end(), [renderable](const RenderEntry& entry) {
                return entry.renderable == renderable;
            });
        if (it != m_renderables.end())
        {
            m_renderScene.Unregister(it->handle);
            m_renderables.erase(it);
        }
    }

    void SceneRenderer::RegisterOverlay(IOverlay* overlay)
    {
        if (overlay == nullptr)
        {
            return;
        }
        if (std::find(m_overlays.begin(), m_overlays.end(), overlay) != m_overlays.end())
        {
            return;
        }

        // 挿入の時点で priority 昇順を保つ。同値は後から来た方が後ろ
        const std::vector<IOverlay*>::iterator at =
            std::upper_bound(m_overlays.begin(), m_overlays.end(), overlay, [](const IOverlay* a, const IOverlay* b) {
                return a->OverlayOrder() < b->OverlayOrder();
            });
        m_overlays.insert(at, overlay);
    }

    void SceneRenderer::UnregisterOverlay(IOverlay* overlay)
    {
        std::erase(m_overlays, overlay);
    }

    void SceneRenderer::RegisterUIActor(UIActor* actor)
    {
        if (actor == nullptr || std::find(m_uiActors.begin(), m_uiActors.end(), actor) != m_uiActors.end())
        {
            return;
        }
        const std::vector<UIActor*>::iterator at =
            std::upper_bound(m_uiActors.begin(), m_uiActors.end(), actor, [](const UIActor* a, const UIActor* b) {
                return a->DrawOrder() < b->DrawOrder();
            });
        m_uiActors.insert(at, actor);
    }

    void SceneRenderer::UnregisterUIActor(UIActor* actor) noexcept
    {
        std::erase(m_uiActors, actor);
    }

    void SceneRenderer::RegisterLight(DirectionalLight* light)
    {
        if (light == nullptr)
        {
            return;
        }
        // 二重に積むと UnregisterLight が片方しか消さず、外したはずの光が残る
        if (std::find(m_lights.begin(), m_lights.end(), light) != m_lights.end())
        {
            return;
        }
        m_lights.push_back(light);
    }

    void SceneRenderer::UnregisterLight(DirectionalLight* light)
    {
        std::erase(m_lights, light);
    }

    void SceneRenderer::SyncRenderBounds()
    {
        // proxy 側の bounds はコピーなので、描画前に登録元の現在値へ揃える
        for (const RenderEntry& entry : m_renderables)
        {
            IRenderable* r = entry.renderable;
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
        for (IOverlay* overlay : m_overlays)
        {
            if (overlay != nullptr && overlay->IsOverlayVisible())
            {
                overlay->OnRenderOverlay(context);
            }
        }
        // 画面に出す物は世界の上の重ね描きよりさらに上。暗転は白の光も覆う
        for (UIActor* actor : m_uiActors)
        {
            actor->OnRenderOverlay(context);
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

    void SceneRenderer::Render(CameraManager& cameras, SceneCamera& camera, std::string_view skyboxPath, float alpha)
    {
        if (m_renderer == nullptr)
        {
            return;
        }

        // ビュー列が空なら現描画先へ CameraManager の視点で 1 回だけ描く。描画先は BeginFrame が bind 済み
        if (m_sceneViews.empty())
        {
            RenderViewWithOverlays(cameras, camera, skyboxPath, SceneView{}, alpha, BloomForView(0));
            return;
        }

        for (std::size_t i = 0; i < m_sceneViews.size(); ++i)
        {
            const SceneView& view = m_sceneViews[i];
            m_renderer->BeginSceneView(view.target);
            RenderViewWithOverlays(cameras, camera, skyboxPath, view, alpha, BloomForView(i));
        }
    }

    NS::Gfx::Bloom& SceneRenderer::BloomForView(std::size_t index)
    {
        while (m_blooms.size() <= index)
        {
            m_blooms.push_back(std::make_unique<NS::Gfx::Bloom>(NS::Gfx::BloomDesc{}));
        }
        return *m_blooms[index];
    }

    void SceneRenderer::RenderViewWithOverlays(CameraManager& cameras,
                                               SceneCamera& camera,
                                               std::string_view skyboxPath,
                                               const SceneView& view,
                                               float alpha,
                                               NS::Gfx::Bloom& bloom)
    {
        const NS::Gfx::RenderContext ctx = RenderWorld(cameras, camera, skyboxPath, view.viewPose, alpha, bloom);

#if !defined(NS_SHIPPING)
        NS::Gfx::DebugDraw::Draw(*ctx.renderer, ctx.viewProjection);

        // ビューの図形は、このビューの行列で積んで描いたらすぐ捨てる。次のビューや次のフレームへ持ち越さない
        if (view.drawShapes)
        {
            m_viewShapes.Clear();
            view.drawShapes(m_viewShapes, ctx.viewProjection);
            m_viewShapes.Draw(*ctx.renderer, ctx.viewProjection);
            m_viewShapes.Clear();
        }
#endif

        DrawOverlays(ctx);
    }

    namespace
    {
        // 歪みの輪を描画先の画素へ直す。中心がカメラの後ろか、描画先が無い時は歪めない
        [[nodiscard]] NS::Gfx::BloomRing ToBloomRing(const NS::Gfx::DistortionRing& ring,
                                                     const NS::Matrix& viewProjection,
                                                     NS::Size2D size) noexcept
        {
            if (!(ring.push > 0.0f) || size.width <= 0 || size.height <= 0)
            {
                return NS::Gfx::BloomRing{};
            }
            const float width = static_cast<float>(size.width);
            const float height = static_cast<float>(size.height);
            NS::Vector2 centerPixel{};
            float centerW = 0.0f;
            if (!NS::Gfx::TryProjectToPixels(viewProjection, ring.center, width, height, centerPixel, centerW))
            {
                return NS::Gfx::BloomRing{};
            }
            return NS::Gfx::BloomRing{.centerPixel = centerPixel,
                                      .radiusPixels = ring.radius * height,
                                      .pushPixels = ring.push * height,
                                      .halfWidthPixels = ring.halfWidth * height};
        }
    } // namespace

    NS::Gfx::RenderContext SceneRenderer::RenderWorld(CameraManager& cameras,
                                                      SceneCamera& camera,
                                                      std::string_view skyboxPath,
                                                      const std::optional<CameraPose>& viewOverride,
                                                      float alpha,
                                                      NS::Gfx::Bloom& bloom)
    {
        // レンダラーの現在サイズから毎回取り直し、リサイズとビュー切替に追従する
        camera.SetAspectRatioFromRenderer(*m_renderer);

        NS::Gfx::RenderContext ctx{};
        ctx.renderer = m_renderer;
        ctx.alpha = alpha;

        // 上書き視点は実カメラを経由せず、その場で行列を組む。実カメラの中身はゲーム視点のまま残す
        NS::CameraData overrideCamera{};
        const NS::CameraData* viewCamera = &camera.Camera();
        if (viewOverride.has_value())
        {
            overrideCamera.SetPosition(viewOverride->position);
            overrideCamera.SetTarget(viewOverride->target);
            overrideCamera.SetUp(viewOverride->up);
            overrideCamera.SetFovY(viewOverride->fovY);
            overrideCamera.SetNearPlane(viewOverride->nearPlane);
            overrideCamera.SetFarPlane(viewOverride->farPlane);

            // aspect は関数の冒頭で renderer から取り直した実カメラの物を写す
            overrideCamera.SetAspectRatio(camera.Camera().AspectRatio());

            ctx.viewProjection = overrideCamera.ViewProjection();
            ctx.cameraPosition = viewOverride->position;
            viewCamera = &overrideCamera;
        }
        else
        {
            ctx.viewProjection = cameras.ViewProjection();
            ctx.cameraPosition = camera.Position();
        }
        ctx.resolvedSettings = ResolveSceneSettings(m_renderer->Settings());
        ctx.groundWave = m_groundWave;
        ctx.distortionRing = m_distortionRing;

        // 物の絵は S 字で 1 以下に書くので、にじむのは 1 を超えて書いたエフェクトだけ
        // 深度は今の描画先の物を使うので、後で描くデバッグの線も世界の深度で隠れる
        bloom.BeginWorld(*m_renderer, m_renderer->Settings().clearColor);
        DrawOpaque(ctx);
        m_renderer->DrawSky(*viewCamera, skyboxPath);
        DrawTransparent(ctx);
        // ワールド空間の半透明物。半透明の後、デバッグ描画の前
        if (m_effects != nullptr)
        {
            m_effects->Draw(*viewCamera);
        }
        // 重ね描きとデバッグの線はにじませないので、この後に今の描画先へ描く
        bloom.EndWorld(*m_renderer, ToBloomRing(ctx.distortionRing, ctx.viewProjection, m_renderer->Size()));
        return ctx;
    }
} // namespace NS::Obj
