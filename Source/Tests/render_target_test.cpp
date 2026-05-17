#include <gtest/gtest.h>

#include <ns/core/logger.h>
#include <ns/graphics/render_target.h>
#include <ns/graphics/renderer.h>
#include <ns/platform/window.h>

namespace
{
    using ns::graphics::Renderer;
    using ns::graphics::RendererDesc;
    using ns::platform::Window;
    using ns::platform::WindowDesc;
} // namespace

class RenderTargetLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { ns::core::Logger::Init(); }
    void TearDown() override { ns::core::Logger::Shutdown(); }
};

TEST_F(RenderTargetLoggerTest, MainRTHasDepth)
{
    WindowDesc wd{};
    wd.title = "ns_rt_depth";
    wd.width = 256;
    wd.height = 256;
    Window window(wd);
    ASSERT_TRUE(window.IsValid());

    RendererDesc rd{};
    rd.vsync = false;
    Renderer renderer(rd, window);
    ASSERT_TRUE(renderer.IsValid());

    auto& rt = renderer.MainRenderTarget();
    EXPECT_TRUE(rt.HasDepth());
}

TEST_F(RenderTargetLoggerTest, ClearIsSafe)
{
    WindowDesc wd{};
    wd.title = "ns_rt_clear";
    Window window(wd);
    ASSERT_TRUE(window.IsValid());

    RendererDesc rd{};
    rd.vsync = false;
    Renderer renderer(rd, window);
    ASSERT_TRUE(renderer.IsValid());

    auto& rt = renderer.MainRenderTarget();
    rt.Clear(0.5f, 0.5f, 0.5f, 1.0f);
    rt.Clear(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    SUCCEED();
}

TEST_F(RenderTargetLoggerTest, BindIsSafe)
{
    WindowDesc wd{};
    wd.title = "ns_rt_bind";
    Window window(wd);
    ASSERT_TRUE(window.IsValid());

    RendererDesc rd{};
    rd.vsync = false;
    Renderer renderer(rd, window);
    ASSERT_TRUE(renderer.IsValid());

    auto& rt = renderer.MainRenderTarget();
    rt.Bind();
    SUCCEED();
}

TEST_F(RenderTargetLoggerTest, ResizeUpdatesSize)
{
    WindowDesc wd{};
    wd.title = "ns_rt_resize";
    wd.width = 320;
    wd.height = 240;
    Window window(wd);
    ASSERT_TRUE(window.IsValid());

    RendererDesc rd{};
    rd.vsync = false;
    Renderer renderer(rd, window);
    ASSERT_TRUE(renderer.IsValid());

    renderer.Resize(640, 480);
    auto& rt = renderer.MainRenderTarget();
    EXPECT_EQ(rt.Width(), 640);
    EXPECT_EQ(rt.Height(), 480);
}
