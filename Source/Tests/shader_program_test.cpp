#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Graphics/ShaderProgram.h>
#include <Framework/Platform/Window.h>

namespace
{
    using NS::Graphics::InputElement;
    using NS::Graphics::InputElementFormat;
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::ShaderProgram;
    using NS::Graphics::ShaderProgramDesc;
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

    /// fallback VS が読む POSITION (float3, offset 0) を含む最小 InputLayout。
    std::vector<InputElement> MakePositionOnlyLayout()
    {
        return {InputElement{"POSITION", InputElementFormat::Float3, 0u}};
    }
} // namespace

class ShaderProgramLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(ShaderProgramLoggerTest, MissingVsPathFallsBack)
{
    Window window(MakeWindowDesc("ns_sp_missing_vs"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    ShaderProgramDesc desc{};
    desc.vertexShaderPath = "C:/nonexistent/__ns_test_missing_vs__.hlsl";
    desc.pixelShaderPath = "C:/nonexistent/__ns_test_missing_ps__.hlsl";
    desc.inputLayout = MakePositionOnlyLayout();

    ShaderProgram sp(renderer, desc);
    EXPECT_TRUE(sp.IsValid());
    EXPECT_TRUE(sp.IsUsingFallback());
}

TEST_F(ShaderProgramLoggerTest, EmptyPathsFallBack)
{
    Window window(MakeWindowDesc("ns_sp_empty"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    ShaderProgramDesc desc{};
    desc.inputLayout = MakePositionOnlyLayout();

    ShaderProgram sp(renderer, desc);
    EXPECT_TRUE(sp.IsValid());
    EXPECT_TRUE(sp.IsUsingFallback());
}

TEST_F(ShaderProgramLoggerTest, EmptyInputLayoutBecomesInvalid)
{
    Window window(MakeWindowDesc("ns_sp_empty_layout"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    // inputLayout 空のまま fallback ルートへ流れ、CreateInputLayout が失敗して IsValid=false で完全クリアされる契約。
    ShaderProgramDesc desc{};
    ShaderProgram sp(renderer, desc);
    EXPECT_FALSE(sp.IsValid());
    EXPECT_FALSE(sp.IsUsingFallback());
}

TEST_F(ShaderProgramLoggerTest, FallbackBindDoesNotCrash)
{
    Window window(MakeWindowDesc("ns_sp_bind"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    ShaderProgramDesc desc{};
    desc.inputLayout = MakePositionOnlyLayout();

    ShaderProgram sp(renderer, desc);
    ASSERT_TRUE(sp.IsValid());

    sp.Bind();
    SUCCEED();
}

TEST_F(ShaderProgramLoggerTest, FallbackAccessorsNonNull)
{
    Window window(MakeWindowDesc("ns_sp_accessors"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    ShaderProgramDesc desc{};
    desc.inputLayout = MakePositionOnlyLayout();

    ShaderProgram sp(renderer, desc);
    ASSERT_TRUE(sp.IsValid());

    EXPECT_NE(NS::Graphics::detail::GetVertexShader(sp), nullptr);
    EXPECT_NE(NS::Graphics::detail::GetPixelShader(sp), nullptr);
    EXPECT_NE(NS::Graphics::detail::GetInputLayout(sp), nullptr);
}
