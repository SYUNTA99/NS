#include <gtest/gtest.h>

#include <Framework/Core/Filesystem.h>
#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Graphics/Shader.h>
#include <Framework/Platform/Window.h>

namespace
{
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::Shader;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    WindowDesc MakeWindowDesc(const char* title)
    {
        WindowDesc d{};
        d.title = title;
        d.size = NS::Math::Size2D{320, 240};
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

class ShaderLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(ShaderLoggerTest, MissingVsFileFallsBack)
{
    Window window(MakeWindowDesc("ns_shader_missing_vs"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    Shader vs(renderer, "C:/nonexistent/__ns_test_missing.vs.hlsl");
    EXPECT_TRUE(vs.IsValid());
    EXPECT_TRUE(vs.IsUsingFallback());
}

TEST_F(ShaderLoggerTest, MissingPsFileFallsBack)
{
    Window window(MakeWindowDesc("ns_shader_missing_ps"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    Shader ps(renderer, "C:/nonexistent/__ns_test_missing.ps.hlsl");
    EXPECT_TRUE(ps.IsValid());
    EXPECT_TRUE(ps.IsUsingFallback());
}

TEST_F(ShaderLoggerTest, UndetectableStageIsInvalid)
{
    Window window(MakeWindowDesc("ns_shader_no_stage"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    // ファイル名に .vs. / .ps. が無いとステージ判定できず IsValid()==false
    Shader s(renderer, "C:/nonexistent/__ns_test_unknown.hlsl");
    EXPECT_FALSE(s.IsValid());
}

TEST_F(ShaderLoggerTest, RealVertexShaderCompiles)
{
    Window window(MakeWindowDesc("ns_shader_real_vs"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto shaderDir = NS::Core::FileSystem::GetExeDirectory() / "Shaders";
    Shader vs(renderer, shaderDir / "standard.vs.hlsl");
    ASSERT_TRUE(vs.IsValid());
    EXPECT_FALSE(vs.IsUsingFallback());

    // 頂点ステージは InputLayout 用の VS バイトコードを持つ
    EXPECT_FALSE(NS::Graphics::detail::GetVertexShaderBytecode(vs).empty());

    renderer.BindShader(vs);
    SUCCEED();
}

TEST_F(ShaderLoggerTest, RealPixelShaderCompiles)
{
    Window window(MakeWindowDesc("ns_shader_real_ps"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto shaderDir = NS::Core::FileSystem::GetExeDirectory() / "Shaders";
    Shader ps(renderer, shaderDir / "player.ps.hlsl");
    ASSERT_TRUE(ps.IsValid());
    EXPECT_FALSE(ps.IsUsingFallback());

    // ピクセルステージは VS バイトコードを持たない
    EXPECT_TRUE(NS::Graphics::detail::GetVertexShaderBytecode(ps).empty());

    renderer.BindShader(ps);
    SUCCEED();
}
