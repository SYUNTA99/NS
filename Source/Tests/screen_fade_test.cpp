#include "NSlib/Graphics/GraphicObject.h"
#include "NSlib/Graphics/RenderTarget.h"
#include "NSlib/Graphics/Renderer.h"
#include "NSlib/Graphics/ScreenFade.h"
#include "NSlib/Graphics/Texture.h"
#include "NSlib/Object/Object.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneRenderer.h"
#include "NSlib/Object/System/UIDirector.h"
#include "NSlib/Object/UIActor.h"
#include "NSlib/Windows/Clock.h"

#include <gtest/gtest.h>

#include <memory>
#include <type_traits>

namespace
{
    class WhiteScreen final : public NS::Obj::UIActor
    {
    public:
        int DrawOrder() const noexcept override { return 10000; }
        void OnRenderOverlay(const NS::Gfx::RenderContext& context) override
        {
            context.renderer->DrawFullscreenColor(NS::Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
    };

    void ExpectEveryPixel(const NS::Gfx::Texture& texture, int expected)
    {
        D3D11_TEXTURE2D_DESC desc{};
        texture.Native()->GetDesc(&desc);
        ASSERT_EQ(desc.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        ASSERT_TRUE(SUCCEEDED(NS::Gfx::Gpu().device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));
        NS::Gfx::Gpu().context->CopyResource(staging.Get(), texture.Native());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        ASSERT_TRUE(SUCCEEDED(NS::Gfx::Gpu().context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        int mismatches = 0;
        for (UINT y = 0; y < desc.Height; ++y)
        {
            const unsigned char* row = static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch;
            for (UINT x = 0; x < desc.Width; ++x)
            {
                for (UINT channel = 0; channel < 3; ++channel)
                {
                    const int value = row[x * 4 + channel];
                    if (value < expected - 1 || value > expected + 1)
                    {
                        ++mismatches;
                    }
                }
            }
        }
        NS::Gfx::Gpu().context->Unmap(staging.Get(), 0);
        EXPECT_EQ(mismatches, 0);
    }
} // namespace

static_assert(!std::is_base_of_v<NS::Obj::Object, NS::Gfx::ScreenFade>);

TEST(ScreenFade, HoldsBlackUntilFadeInAndCanCancel)
{
    NS::Gfx::ScreenFade fade;
    EXPECT_FLOAT_EQ(fade.Alpha(), 0.0f);
    fade.BeginOut(0.4f);
    fade.Advance(0.1f);
    EXPECT_FLOAT_EQ(fade.Alpha(), 0.25f);
    fade.Advance(0.3f);
    ASSERT_TRUE(fade.IsBlack());
    fade.Advance(1.0f);
    EXPECT_FLOAT_EQ(fade.Alpha(), 1.0f);
    fade.BeginIn(0.4f);
    fade.Advance(0.1f);
    EXPECT_FLOAT_EQ(fade.Alpha(), 0.75f);
    fade.Cancel();
    EXPECT_FALSE(fade.IsFading());
    EXPECT_FALSE(fade.IsBlack());
    EXPECT_FLOAT_EQ(fade.Alpha(), 0.0f);
    fade.BeginOut(0.4f);
    fade.Advance(0.1f);
    EXPECT_FLOAT_EQ(fade.Alpha(), 0.25f);
}

TEST(ScreenFade, ZeroDurationChangesImmediately)
{
    NS::Gfx::ScreenFade fade;
    fade.BeginOut(0.0f);
    EXPECT_TRUE(fade.IsBlack());
    EXPECT_FLOAT_EQ(fade.Alpha(), 1.0f);
    fade.BeginIn(0.0f);
    EXPECT_FALSE(fade.IsFading());
    EXPECT_FLOAT_EQ(fade.Alpha(), 0.0f);
}

TEST(ScreenFade, RepeatedRequestsDoNotRestartTheTransition)
{
    NS::Gfx::ScreenFade fade;
    fade.BeginIn(1.0f);
    EXPECT_FLOAT_EQ(fade.Alpha(), 0.0f);
    fade.BeginOut(0.4f);
    fade.Advance(0.1f);
    fade.BeginOut(1.0f);
    fade.BeginIn(1.0f);
    fade.Advance(0.1f);
    EXPECT_FLOAT_EQ(fade.Alpha(), 0.5f);
    fade.Advance(0.2f);
    fade.BeginIn(0.4f);
    fade.Advance(0.1f);
    fade.BeginIn(1.0f);
    fade.Advance(0.3f);
    EXPECT_FLOAT_EQ(fade.Alpha(), 0.0f);
    EXPECT_FALSE(fade.IsFading());
}

TEST(ScreenFade, SceneAdvancesOncePerStepWithoutWorldTimeOrAnActor)
{
    NS::Obj::Scene scene;
    scene.SetWorldSpeed(0.0f);
    scene.HoldOthers(10);
    scene.Fade().BeginOut(NS::OS::FrameTimer::FixedDelta() * 4.0f);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.25f);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.5f);
    EXPECT_EQ(scene.Objects().ObjectCount(), 0u);

    NS::Obj::SceneRenderer renderer;
    renderer.Fade().BeginOut(0.0f);
    EXPECT_TRUE(renderer.Fade().IsBlack());
}

TEST(ScreenFade, ScenePauseAndDisabledSimulationFreezeTheFade)
{
    NS::Obj::Scene scene;
    scene.Fade().BeginOut(NS::OS::FrameTimer::FixedDelta() * 4.0f);
    scene.OnUpdate();
    scene.SetSimulationPaused(true);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.25f);
    scene.StepSimulation();
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.5f);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.5f);
    scene.SetSimulationEnabled(false);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.5f);
    scene.SetSimulationEnabled(true);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.75f);
}

TEST(ScreenFade, SceneReloadAndShutdownCancelTheFade)
{
    NS::Obj::Scene scene;
    scene.Fade().BeginOut(0.0f);
    scene.LoadJson(NS::Obj::MakeSceneJson());
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.0f);
    scene.Fade().BeginOut(0.0f);
    scene.OnShutdown();
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.0f);

    NS::Obj::Scene other;
    other.Fade().BeginOut(0.0f);
    EXPECT_FLOAT_EQ(scene.Fade().Alpha(), 0.0f);
    EXPECT_TRUE(other.Fade().IsBlack());
}

TEST(ScreenFade, FullscreenDrawCoversUIAndTheResizedTarget)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "全画面暗転の試し", .size = {64, 36}, .visible = false});
    NS::Gfx::Renderer renderer(NS::Gfx::RendererDesc{}, window);
    ASSERT_TRUE(renderer.IsValid());
    const std::unique_ptr<NS::Gfx::RenderTarget> target = NS::Gfx::RenderTarget::Create({64, 36});
    ASSERT_TRUE(target->IsValid());
    NS::Obj::SceneRenderer sceneRenderer;
    WhiteScreen ui;
    NS::Obj::UIDirector screens;
    screens.Add(ui);
    NS::Gfx::RenderContext context;
    context.renderer = &renderer;

    renderer.SetSceneTarget(target.get());
    sceneRenderer.Fade().BeginOut(0.0f);
    renderer.BeginFrame();
    sceneRenderer.DrawOverlays(context, screens);
    ExpectEveryPixel(*target->Color(), 0);

    sceneRenderer.Fade().BeginIn(0.4f);
    sceneRenderer.Fade().Advance(0.2f);
    target->Resize({37, 53});
    renderer.BeginFrame();
    sceneRenderer.DrawOverlays(context, screens);
    ExpectEveryPixel(*target->Color(), 128);

    sceneRenderer.Fade().Cancel();
    renderer.BeginFrame();
    sceneRenderer.DrawOverlays(context, screens);
    ExpectEveryPixel(*target->Color(), 255);
    renderer.SetSceneTarget(nullptr);
    renderer.BindBackbuffer();
}
