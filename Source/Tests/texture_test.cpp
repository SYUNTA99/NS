#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Graphics/Texture.h>
#include <Framework/Platform/Window.h>

namespace
{
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::ShaderStage;
    using NS::Graphics::Texture;
    using NS::Graphics::TextureDesc;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    WindowDesc MakeWindowDesc(const char* title)
    {
        WindowDesc d{};
        d.title = title;
        d.size = NS::Core::Size2D{320, 240};
        d.visible = false;
        return d;
    }

    RendererDesc MakeRendererDesc()
    {
        RendererDesc d{};
        d.vsync = false;
        d.enableDebugLayer = false;
        return d;
    }
} // namespace

class TextureLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(TextureLoggerTest, MissingFileFallsBackToMagenta)
{
    Window window(MakeWindowDesc("ns_tex_missing"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    TextureDesc desc{};
    desc.path = "C:/nonexistent/__ns_test_missing__.png";

    Texture tex(renderer, desc);
    EXPECT_TRUE(tex.IsValid());
    EXPECT_TRUE(tex.IsUsingFallback());
    EXPECT_EQ(tex.Size(), (NS::Core::Size2D{1, 1}));
}

TEST_F(TextureLoggerTest, EmptyPathFallsBack)
{
    Window window(MakeWindowDesc("ns_tex_empty"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    TextureDesc desc{};
    Texture tex(renderer, desc);
    EXPECT_TRUE(tex.IsValid());
    EXPECT_TRUE(tex.IsUsingFallback());
}

TEST_F(TextureLoggerTest, FallbackBindDoesNotCrash)
{
    Window window(MakeWindowDesc("ns_tex_bind"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    TextureDesc desc{};
    desc.path = "C:/nonexistent/__ns_test_bind__.png";
    Texture tex(renderer, desc);
    ASSERT_TRUE(tex.IsValid());

    tex.Bind(0);
    tex.Bind(1, ShaderStage::Pixel);
    tex.Bind(0, ShaderStage::Vertex | ShaderStage::Pixel);
    SUCCEED();
}

TEST_F(TextureLoggerTest, SimpleOverloadConstructs)
{
    Window window(MakeWindowDesc("ns_tex_overload"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    Texture tex(renderer, "C:/nonexistent/__ns_test_overload__.png");
    EXPECT_TRUE(tex.IsValid());
    EXPECT_TRUE(tex.IsUsingFallback());
}

TEST_F(TextureLoggerTest, FallbackSrvIsNonNull)
{
    Window window(MakeWindowDesc("ns_tex_srv"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    TextureDesc desc{};
    desc.path = "C:/nonexistent/__ns_test_srv__.png";
    Texture tex(renderer, desc);
    ASSERT_TRUE(tex.IsValid());

    EXPECT_NE(NS::Graphics::detail::GetSrv(tex), nullptr);
}
