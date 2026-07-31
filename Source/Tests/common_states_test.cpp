#include <gtest/gtest.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/CommonStates.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Platform/Window.h>

namespace
{
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;
} // namespace

class CommonStatesLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(CommonStatesLoggerTest, AllGettersAreNonNull)
{
    WindowDesc wd{};
    wd.title = "ns_common_states";
    wd.visible = false;
    Window window(wd);
    ASSERT_TRUE(window.IsValid());

    RendererDesc rd{};
    rd.vsync = false;
    Renderer renderer(rd, window);
    ASSERT_TRUE(renderer.IsValid());

    auto& s = renderer.States();
    EXPECT_NE(s.Opaque(), nullptr);
    EXPECT_NE(s.AlphaBlend(), nullptr);
    EXPECT_NE(s.DepthDefault(), nullptr);
    EXPECT_NE(s.DepthNone(), nullptr);
    EXPECT_NE(s.CullCounterClockwise(), nullptr);
    EXPECT_NE(s.CullClockwise(), nullptr);
    EXPECT_NE(s.LinearWrap(), nullptr);
    EXPECT_NE(s.LinearClamp(), nullptr);
    EXPECT_NE(s.PointWrap(), nullptr);
    EXPECT_NE(s.PointClamp(), nullptr);
}

TEST_F(CommonStatesLoggerTest, RepeatedGettersReturnSameInstance)
{
    WindowDesc wd{};
    wd.title = "ns_common_states_cache";
    wd.visible = false;
    Window window(wd);
    ASSERT_TRUE(window.IsValid());

    RendererDesc rd{};
    rd.vsync = false;
    Renderer renderer(rd, window);
    ASSERT_TRUE(renderer.IsValid());

    auto& s = renderer.States();
    EXPECT_EQ(s.Opaque(), s.Opaque());
    EXPECT_EQ(s.LinearWrap(), s.LinearWrap());
    EXPECT_EQ(s.DepthDefault(), s.DepthDefault());
}
