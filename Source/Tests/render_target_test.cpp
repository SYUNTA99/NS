#include <gtest/gtest.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Graphics/RenderTarget.h>
#include <Runtime/Platform/Window.h>

namespace
{
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::RenderTarget;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    WindowDesc MakeDesc(const char* title, int width = 320, int height = 240)
    {
        WindowDesc d{};
        d.title = title;
        d.size = NS::Math::Size2D{width, height};
        d.visible = false;
        return d;
    }

    RendererDesc MakeRendererDesc()
    {
        RendererDesc d{};
#ifdef NS_BUILD_DEBUG
        d.enableDebugLayer = true;
#else
        d.enableDebugLayer = false;
#endif
        d.vsync = false;
        return d;
    }
} // namespace

class RenderTargetLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(RenderTargetLoggerTest, CreateBuildsColorAndDepth)
{
    Window window(MakeDesc("ns_render_target_create"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto target = RenderTarget::Create(NS::Math::Size2D{256, 128});
    ASSERT_NE(target, nullptr);
    EXPECT_TRUE(target->IsValid());
    EXPECT_EQ(target->Size(), (NS::Math::Size2D{256, 128}));
    EXPECT_NE(target->UiTextureHandle(), nullptr);
}

TEST_F(RenderTargetLoggerTest, ResizeRebuildsToNewSize)
{
    Window window(MakeDesc("ns_render_target_resize"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto target = RenderTarget::Create(NS::Math::Size2D{256, 128});
    ASSERT_TRUE(target->IsValid());

    target->Resize(NS::Math::Size2D{512, 256});
    EXPECT_EQ(target->Size(), (NS::Math::Size2D{512, 256}));
    EXPECT_NE(target->UiTextureHandle(), nullptr);
}

TEST_F(RenderTargetLoggerTest, ResizeSameSizeKeepsTexture)
{
    Window window(MakeDesc("ns_render_target_resize_same"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto target = RenderTarget::Create(NS::Math::Size2D{256, 128});
    ASSERT_TRUE(target->IsValid());

    void* const before = target->UiTextureHandle();
    target->Resize(NS::Math::Size2D{256, 128});
    EXPECT_EQ(target->UiTextureHandle(), before);
}

TEST_F(RenderTargetLoggerTest, ResizeZeroIsIgnored)
{
    Window window(MakeDesc("ns_render_target_resize_zero"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto target = RenderTarget::Create(NS::Math::Size2D{256, 128});
    ASSERT_TRUE(target->IsValid());

    void* const before = target->UiTextureHandle();
    target->Resize(NS::Math::Size2D{0, 0});
    EXPECT_EQ(target->Size(), (NS::Math::Size2D{256, 128}));
    EXPECT_EQ(target->UiTextureHandle(), before);
}

TEST_F(RenderTargetLoggerTest, SetSceneTargetSwitchesRendererSize)
{
    Window window(MakeDesc("ns_render_target_scene", 320, 240));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto target = RenderTarget::Create(NS::Math::Size2D{256, 128});
    ASSERT_TRUE(target->IsValid());

    renderer.SetSceneTarget(target.get());
    EXPECT_EQ(renderer.Size(), (NS::Math::Size2D{256, 128}));

    // オフスクリーン設定中も 1 フレーム分の切替が安全に通ること
    renderer.BeginFrame(0.1f, 0.2f, 0.3f, 1.0f);
    renderer.BindBackbuffer();
    renderer.EndFrame();

    renderer.SetSceneTarget(nullptr);
    EXPECT_EQ(renderer.Size(), (NS::Math::Size2D{320, 240}));

    renderer.BeginFrame();
    renderer.EndFrame();
    SUCCEED();
}
