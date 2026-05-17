#include <gtest/gtest.h>

#include <ns/core/logger.h>
#include <ns/graphics/common_states.h>
#include <ns/graphics/render_target.h>
#include <ns/graphics/renderer.h>
#include <ns/platform/window.h>

namespace
{
    using ns::graphics::Renderer;
    using ns::graphics::RendererDesc;
    using ns::platform::Window;
    using ns::platform::WindowDesc;

    WindowDesc MakeDesc(const char* title, int width = 320, int height = 240)
    {
        WindowDesc d{};
        d.title = title;
        d.width = width;
        d.height = height;
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
    void SetUp() override { ns::core::Logger::Init(); }
    void TearDown() override { ns::core::Logger::Shutdown(); }
};

TEST_F(RendererLoggerTest, ConstructsAndIsValid)
{
    Window window(MakeDesc("ns_renderer_construct"));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    EXPECT_TRUE(renderer.IsValid());
}

TEST_F(RendererLoggerTest, WidthHeightMatchesWindow)
{
    Window window(MakeDesc("ns_renderer_size", 512, 384));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());
    EXPECT_EQ(renderer.Width(), 512);
    EXPECT_EQ(renderer.Height(), 384);
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

    renderer.Resize(800, 600);
    EXPECT_EQ(renderer.Width(), 800);
    EXPECT_EQ(renderer.Height(), 600);
}

TEST_F(RendererLoggerTest, ResizeZeroIsNoop)
{
    Window window(MakeDesc("ns_renderer_resize_zero"));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const int w = renderer.Width();
    const int h = renderer.Height();
    renderer.Resize(0, 0);
    EXPECT_EQ(renderer.Width(), w);
    EXPECT_EQ(renderer.Height(), h);
}

TEST_F(RendererLoggerTest, MainRenderTargetIsAccessible)
{
    Window window(MakeDesc("ns_renderer_mrt", 400, 300));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    auto& rt = renderer.MainRenderTarget();
    EXPECT_EQ(rt.Width(), 400);
    EXPECT_EQ(rt.Height(), 300);
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
