#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/CommonStates.h>
#include <Framework/Graphics/RenderTarget.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Platform/Window.h>

namespace
{
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    WindowDesc MakeDesc(const char* title, int width = 320, int height = 240)
    {
        WindowDesc d{};
        d.title = title;
        d.size = NS::Core::Size2D{width, height};
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

class RendererLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(RendererLoggerTest, ConstructsAndIsValid)
{
    Window window(MakeDesc("ns_renderer_construct"));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    EXPECT_TRUE(renderer.IsValid());
}

TEST_F(RendererLoggerTest, SizeMatchesWindow)
{
    Window window(MakeDesc("ns_renderer_size", 512, 384));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());
    EXPECT_EQ(renderer.Size(), (NS::Core::Size2D{512, 384}));
}

TEST_F(RendererLoggerTest, BeginEndFrameIsSafe)
{
    Window window(MakeDesc("ns_renderer_frame"));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    renderer.BeginFrame(0.1f, 0.2f, 0.3f, 1.0f);
    renderer.EndFrame();
    renderer.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    renderer.EndFrame();
    SUCCEED();
}

TEST_F(RendererLoggerTest, ResizeUpdatesSize)
{
    Window window(MakeDesc("ns_renderer_resize", 320, 240));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    renderer.Resize(NS::Core::Size2D{800, 600});
    EXPECT_EQ(renderer.Size(), (NS::Core::Size2D{800, 600}));
}

TEST_F(RendererLoggerTest, ResizeZeroIsNoop)
{
    Window window(MakeDesc("ns_renderer_resize_zero"));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const NS::Core::Size2D before = renderer.Size();
    renderer.Resize(NS::Core::Size2D{0, 0});
    EXPECT_EQ(renderer.Size(), before);
}

TEST_F(RendererLoggerTest, MainRenderTargetIsAccessible)
{
    Window window(MakeDesc("ns_renderer_mrt", 400, 300));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    auto& rt = renderer.MainRenderTarget();
    EXPECT_EQ(rt.Size(), (NS::Core::Size2D{400, 300}));
    EXPECT_TRUE(rt.HasDepth());
}

TEST_F(RendererLoggerTest, StatesAreAccessible)
{
    Window window(MakeDesc("ns_renderer_states"));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    auto& states = renderer.States();
    EXPECT_NE(states.Opaque(), nullptr);
    EXPECT_NE(states.DepthDefault(), nullptr);
    EXPECT_NE(states.CullCounterClockwise(), nullptr);
    EXPECT_NE(states.LinearWrap(), nullptr);
}
