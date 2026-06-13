#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/CommonStates.h>
#include <Framework/Graphics/Pipeline.h>
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

TEST_F(RendererLoggerTest, CommonPipelinesAreValidDistinctAndCached)
{
    Window window(MakeDesc("ns_renderer_pipelines"));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const NS::Graphics::Pipeline& opaque = renderer.CommonPipeline(NS::Graphics::BlendMode::Opaque);
    const NS::Graphics::Pipeline& alpha = renderer.CommonPipeline(NS::Graphics::BlendMode::Alpha);
    const NS::Graphics::Pipeline& additive = renderer.CommonPipeline(NS::Graphics::BlendMode::Additive);

    EXPECT_TRUE(opaque.IsValid());
    EXPECT_TRUE(alpha.IsValid());
    EXPECT_TRUE(additive.IsValid());

    // BlendMode ごとに別インスタンス
    EXPECT_NE(&opaque, &alpha);
    EXPECT_NE(&opaque, &additive);
    EXPECT_NE(&alpha, &additive);

    // create-once: 再取得で同一インスタンスを返す
    EXPECT_EQ(&opaque, &renderer.CommonPipeline(NS::Graphics::BlendMode::Opaque));

    // desc は意図どおり (半透明は深度読取専用)
    EXPECT_EQ(opaque.Desc().blend, NS::Graphics::BlendMode::Opaque);
    EXPECT_EQ(opaque.Desc().depth, NS::Graphics::DepthMode::ReadWrite);
    EXPECT_EQ(alpha.Desc().blend, NS::Graphics::BlendMode::Alpha);
    EXPECT_EQ(alpha.Desc().depth, NS::Graphics::DepthMode::ReadOnly);
    EXPECT_EQ(additive.Desc().blend, NS::Graphics::BlendMode::Additive);
    EXPECT_EQ(additive.Desc().depth, NS::Graphics::DepthMode::ReadOnly);
}

TEST_F(RendererLoggerTest, SizeMatchesWindow)
{
    Window window(MakeDesc("ns_renderer_size", 512, 384));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());
    EXPECT_EQ(renderer.Size(), (NS::Math::Size2D{512, 384}));
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

    renderer.Resize(NS::Math::Size2D{800, 600});
    EXPECT_EQ(renderer.Size(), (NS::Math::Size2D{800, 600}));
}

TEST_F(RendererLoggerTest, ResizeZeroIsNoop)
{
    Window window(MakeDesc("ns_renderer_resize_zero"));
    ASSERT_TRUE(window.IsValid());

    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const NS::Math::Size2D before = renderer.Size();
    renderer.Resize(NS::Math::Size2D{0, 0});
    EXPECT_EQ(renderer.Size(), before);
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
