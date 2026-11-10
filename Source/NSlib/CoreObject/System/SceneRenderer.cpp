#include "NSlib/CoreObject/System/SceneRenderer.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/DirectionalLight.h"
#include "NSlib/CoreObject/ClassObjs/OverlayRenderer.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Shadow.h"
#include "NSlib/Core/Frustum.h"
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
#include "NSlib/Gfx/Render/SceneDepthCopy.h"
#include "NSlib/Gfx/Render/Skybox.h"
#include "NSlib/Gfx/D3DBuffer.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/Render/DrawConstants.h"
#include "NSlib/Gfx/D3DCommonStates.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Core/Windows/Clock.h"

#include <algorithm>
#include <string>
#include <utility>

namespace NS
{
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
            m_afterSkyDepth.reset();
            m_viewConstants.reset();
            m_debugShapes.reset();
            m_shaders = Gfx::BuiltinShaders{};
            return;
        }

        // 描く時は AssetManager を引かない決まりなので、差した時に 1 回だけ引いて描く段へ渡す
        m_shaders = AssetManager::Get().LoadBuiltinShaders();
        m_overlay = std::make_unique<Gfx::ScreenOverlay>(device, m_shaders);
        m_skybox = std::make_unique<Gfx::Skybox>(m_shaders);
        m_afterSkyDepth = std::make_unique<Gfx::SceneDepthCopy>();
        m_viewConstants = Gfx::D3DBuffer::Create(Gfx::MakeConstantBufferDesc(sizeof(Gfx::ViewConstants)));
        if (!m_viewConstants->IsValid())
        {
            NS_LOG_ERROR(Graphics, "SceneRenderer: ビューごとの定数バッファを作れない (byteSize={})", sizeof(Gfx::ViewConstants));
            m_viewConstants.reset();
        }
        // 資源は今の装置の物なので、装置を差す度に作り直す。出荷では線を描かないので作らない
#if !defined(NS_SHIPPING)
        m_debugShapes = std::make_unique<Gfx::DebugShapePass>(m_shaders);
#endif

        m_effects = std::make_unique<Gfx::EffectScene>();
    }

    void SceneRenderer::SetSceneValue(std::size_t index, const Vector4& value) noexcept
    {
        NS_ASSERT(Graphics, index < m_sceneValues.size(), "SceneRenderer::SetSceneValue: 番号が欄の数を超える");
        m_sceneValues[index] = value;
    }

    void SceneRenderer::OnTick()
    {
        m_sceneTimeSeconds += OS::FrameTimer::FixedDelta();
        if (m_effects == nullptr)
        {
            return;
        }
        m_effects->Update(OS::FrameTimer::FixedDelta());
    }

    namespace
    {
        // part の組の番号。入れていなければ組の数
        template <typename Entries, typename Part>
        [[nodiscard]] std::size_t FindDrawEntry(const Entries& entries, const Part* part) noexcept
        {
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                if (entries[i].part == part)
                {
                    return i;
                }
            }
            return entries.size();
        }

        template <typename Entries, typename Part>
        void SetDrawEntryEnabled(Entries& entries, const Part* part, bool enabled) noexcept
        {
            const std::size_t index = FindDrawEntry(entries, part);
            if (index < entries.size())
            {
                entries[index].enabled = enabled;
            }
        }

        template <typename Entries>
        [[nodiscard]] std::size_t CountEnabled(const Entries& entries) noexcept
        {
            std::size_t count = 0;
            for (const typename Entries::value_type& entry : entries)
            {
                if (entry.enabled)
                {
                    ++count;
                }
            }
            return count;
        }
    } // namespace

    void SceneRenderer::RegisterModel(Model* model, const Transform* root)
    {
        NS_ASSERT(Graphics, model != nullptr, "SceneRenderer::RegisterModel: Model が null");
        NS_ASSERT(Graphics, root != nullptr, "SceneRenderer::RegisterModel: 持ち主の根が null");
        if (model == nullptr || root == nullptr || FindDrawEntry(m_models, model) < m_models.size())
        {
            return;
        }
        m_models.push_back(DrawEntry<Model>{.part = model, .root = root, .order = m_nextOrder++});
    }

    void SceneRenderer::UnregisterModel(const Model* model) noexcept
    {
        std::erase_if(m_models, [model](const DrawEntry<Model>& entry) { return entry.part == model; });
        std::erase(m_bodyMask, model);
    }

    void SceneRenderer::SetModelEnabled(const Model* model, bool enabled) noexcept
    {
        NS_ASSERT(Scene, model != nullptr, "SetModelEnabled: Model が null");
        SetDrawEntryEnabled(m_models, model, enabled);
    }

    void SceneRenderer::RegisterShadow(Shadow* shadow, const Transform* root)
    {
        NS_ASSERT(Graphics, shadow != nullptr, "SceneRenderer::RegisterShadow: Shadow が null");
        NS_ASSERT(Graphics, root != nullptr, "SceneRenderer::RegisterShadow: 持ち主の根が null");
        if (shadow == nullptr || root == nullptr || FindDrawEntry(m_shadows, shadow) < m_shadows.size())
        {
            return;
        }
        m_shadows.push_back(DrawEntry<Shadow>{.part = shadow, .root = root, .order = m_nextOrder++});
    }

    void SceneRenderer::UnregisterShadow(const Shadow* shadow) noexcept
    {
        std::erase_if(m_shadows, [shadow](const DrawEntry<Shadow>& entry) { return entry.part == shadow; });
    }

    void SceneRenderer::SetShadowEnabled(const Shadow* shadow, bool enabled) noexcept
    {
        NS_ASSERT(Scene, shadow != nullptr, "SetShadowEnabled: Shadow が null");
        SetDrawEntryEnabled(m_shadows, shadow, enabled);
    }

    std::size_t SceneRenderer::EnabledModelCount() const noexcept { return CountEnabled(m_models); }

    std::size_t SceneRenderer::EnabledShadowCount() const noexcept { return CountEnabled(m_shadows); }

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

    void SceneRenderer::GatherVisible(const Gfx::RenderContext& context, Gfx::RenderPass pass)
    {
        const Frustum frustum = Frustum::FromViewProjection(context.viewProjection);
        m_visibleScratch.clear();
        const auto keep = [&](VisibleDraw::Kind kind, std::size_t index, const Gfx::RenderBounds& bounds, uint64 order) {
            // 視錐台の外は DrawItem を作らない
            if (bounds.pass == pass && frustum.Intersects(bounds.bounds))
            {
                m_visibleScratch.push_back(VisibleDraw{.kind = kind,
                                                       .index = index,
                                                       .sortCenter = bounds.sortCenter,
                                                       .sortPriority = bounds.sortPriority,
                                                       .order = order});
            }
        };
        // 箱と並べる値は今の固定フレームの根から作る。隠した組は箱を読まずに飛ばす
        for (std::size_t i = 0; i < m_models.size(); ++i)
        {
            const DrawEntry<Model>& entry = m_models[i];
            if (!entry.enabled)
            {
                continue;
            }
            const Matrix root = entry.root->WorldMatrix();
            if (entry.part->DrawsBody())
            {
                keep(VisibleDraw::Kind::ModelBody, i, entry.part->Bounds(root), entry.order);
            }
            else if (entry.part->DrawsCopies())
            {
                keep(VisibleDraw::Kind::ModelCopies, i, entry.part->CopiesBounds(root), entry.order);
            }
        }
        for (std::size_t i = 0; i < m_shadows.size(); ++i)
        {
            const DrawEntry<Shadow>& entry = m_shadows[i];
            if (entry.enabled)
            {
                keep(VisibleDraw::Kind::Shadow, i, entry.part->Bounds(entry.root->WorldMatrix()), entry.order);
            }
        }

        if (pass != Gfx::RenderPass::Transparent)
        {
            std::ranges::sort(m_visibleScratch, {}, &VisibleDraw::order);
            return;
        }
        const Vector3 camera = context.cameraPosition;
        std::ranges::sort(m_visibleScratch, [camera](const VisibleDraw& a, const VisibleDraw& b) noexcept {
            const float da = (a.sortCenter - camera).LengthSquared();
            const float db = (b.sortCenter - camera).LengthSquared();
            if (da != db)
            {
                return da > db; // 遠い順に描かないと半透明の重なりが崩れる
            }
            if (a.sortPriority != b.sortPriority)
            {
                return a.sortPriority < b.sortPriority;
            }
            return a.order < b.order;
        });
    }

    void SceneRenderer::CollectPass(const Gfx::RenderContext& context,
                                    Gfx::RenderPass pass,
                                    std::vector<Gfx::DrawItem>& out)
    {
        GatherVisible(context, pass);
        for (const VisibleDraw& visible : m_visibleScratch)
        {
            switch (visible.kind)
            {
                case VisibleDraw::Kind::ModelBody:
                {
                    const DrawEntry<Model>& entry = m_models[visible.index];
                    entry.part->Collect(context, entry.root->InterpolatedWorldMatrix(context.alpha), out);
                    break;
                }
                case VisibleDraw::Kind::ModelCopies:
                {
                    const DrawEntry<Model>& entry = m_models[visible.index];
                    entry.part->CollectCopies(context, entry.root->InterpolatedWorldMatrix(context.alpha), out);
                    break;
                }
                case VisibleDraw::Kind::Shadow:
                {
                    const DrawEntry<Shadow>& entry = m_shadows[visible.index];
                    entry.part->Collect(context, entry.root->InterpolatedWorldMatrix(context.alpha), out);
                    break;
                }
            }
        }
    }

    void SceneRenderer::IssueDrawScratch(const Gfx::RenderContext& context)
    {
        if (context.device == nullptr)
        {
            return;
        }
        for (const Gfx::DrawItem& item : m_drawScratch)
        {
            Gfx::IssueDrawItem(*context.device, item);
        }
    }

    void SceneRenderer::DrawOpaque(const Gfx::RenderContext& context)
    {
        m_drawScratch.clear();
        CollectPass(context, Gfx::RenderPass::Opaque, m_drawScratch);
        IssueDrawScratch(context);
    }

    void SceneRenderer::DrawAfterSky(const Gfx::RenderContext& context)
    {
        m_drawScratch.clear();
        CollectPass(context, Gfx::RenderPass::AfterSky, m_drawScratch);
        if (m_drawScratch.empty())
        {
            return;
        }
        const Gfx::SceneDepthCopy::CaptureResult depth = m_afterSkyDepth->Capture();
        // 描くたびに失敗するので、続いている間は 1 回だけ出す
        if (!depth.failure.empty() && !m_afterSkyDepthFailureLogged)
        {
            NS_LOG_ERROR(Graphics, "SceneRenderer: {} ({}x{})。深度なしで描く", depth.failure, depth.width, depth.height);
        }
        m_afterSkyDepthFailureLogged = !depth.failure.empty();
        Gfx::D3DCommandList& commands = m_device->Commands();
        if (depth.texture != nullptr)
        {
            commands.PSSetShaderResource(*depth.texture, Gfx::k_SceneDepthSlot);
        }
        if (const Gfx::D3DTexture* sky = m_skybox->Cubemap())
        {
            commands.PSSetShaderResource(*sky, Gfx::k_SkyCubeSlot);
        }
        commands.PSSetSampler(m_device->States().LinearClamp(), Gfx::k_SkySamplerSlot);
        IssueDrawScratch(context);
        commands.UnbindPixelShaderResources(Gfx::k_SceneDepthSlot, 2);
    }

    void SceneRenderer::DrawTransparent(const Gfx::RenderContext& context)
    {
        m_drawScratch.clear();
        CollectPass(context, Gfx::RenderPass::Transparent, m_drawScratch);
        IssueDrawScratch(context);
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
        m_viewConstantsSent = 0;

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
        for (const Model* model : m_bodyMask)
        {
            const std::size_t index = FindDrawEntry(m_models, model);
            if (index < m_models.size() && m_models[index].enabled && model->DrawsBody())
            {
                model->Collect(context, m_models[index].root->InterpolatedWorldMatrix(context.alpha), m_bodyMaskItems);
            }
        }
        for (const Gfx::DrawItem& item : m_bodyMaskItems)
        {
            screen.DrawBodyMaskItem(*m_device, item);
        }
        screen.EndBodyMask(*m_device);
    }

    void SceneRenderer::SendViewConstants(const Gfx::RenderContext& context)
    {
        if (m_viewConstants == nullptr)
        {
            return;
        }
        // context.resolvedSettings は project 既定に配置された平行光まで解決済
        const Gfx::RenderSettings& settings = context.resolvedSettings;
        Gfx::ViewConstants view{};
        view.viewProj = context.viewProjection;
        view.lightDir = settings.lightDir;
        view.lightDir.Normalize();
        view.lightColor = settings.lightColor;
        view.ambientColor = settings.ambientColor;
        view.groundColor = settings.groundColor;
        view.exposure = settings.exposure;
        view.sceneValues = context.sceneValues;
        view.cameraPosition = context.cameraPosition;
        view.sceneTimeSeconds = context.sceneTimeSeconds;
        m_device->Commands().UpdateSubresource(*m_viewConstants, &view, sizeof(view));
        ++m_viewConstantsSent;
        BindViewConstants();
    }

    void SceneRenderer::BindViewConstants()
    {
        if (m_viewConstants == nullptr)
        {
            return;
        }
        m_device->Commands().VSSetConstantBuffer(*m_viewConstants, Gfx::k_ViewConstantsSlot);
        m_device->Commands().PSSetConstantBuffer(*m_viewConstants, Gfx::k_ViewConstantsSlot);
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
        m_debugShapes->Draw(*ctx.device, Gfx::DebugDrawStepShapes(), ctx.viewProjection);

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
            if (!IsPositive(ring.push) || size.width <= 0 || size.height <= 0)
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
        ctx.sceneTimeSeconds = m_sceneTimeSeconds;

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
        ctx.sceneValues = m_sceneValues;
        ctx.distortionRing = m_distortionRing;
        SendViewConstants(ctx);

        // 物の絵は S 字で 1 以下に書くので、にじむのは 1 を超えて書いたエフェクトだけ
        // 深度は今の描画先の物を使うので、後で描くデバッグの線も世界の深度で隠れる
        passes.bloom->BeginWorld(*m_device, m_device->ClearColor());
        DrawOpaque(ctx);
        m_skybox->Draw(*m_device, *viewCamera, skyboxPath);
        DrawAfterSky(ctx);
        DrawTransparent(ctx);
        // ワールド空間の半透明物。半透明の後、デバッグ描画の前
        if (m_effects != nullptr)
        {
            m_effects->Draw(*viewCamera, m_sceneTimeSeconds);
        }
        // 重ね描きとデバッグの線はにじませも歪ませもしないので、この後に今の描画先へ描く
        passes.bloom->EndWorld(*m_device);
        BindViewConstants();
        DrawBodyMask(ctx, *passes.screen);
        passes.screen->Distort(*m_device, ToScreenRing(ctx.distortionRing, ctx.viewProjection, m_device->Size()));
        return ctx;
    }
} // namespace NS
