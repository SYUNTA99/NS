#include <gtest/gtest.h>

#include <ns/core/logger.h>
#include <ns/graphics/renderer.h>
#include <ns/graphics/shader_program.h>
#include <ns/platform/window.h>

namespace
{
    using ns::graphics::InputElement;
    using ns::graphics::InputElementFormat;
    using ns::graphics::Renderer;
    using ns::graphics::RendererDesc;
    using ns::graphics::ShaderProgram;
    using ns::graphics::ShaderProgramDesc;
    using ns::platform::Window;
    using ns::platform::WindowDesc;

    WindowDesc MakeWindowDesc(const char* title)
    {
        WindowDesc d{};
        d.title = title;
        d.width = 320;
        d.height = 240;
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
    void SetUp() override { ns::core::Logger::Init(); }
    void TearDown() override { ns::core::Logger::Shutdown(); }
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

    EXPECT_NE(ns::graphics::detail::GetVertexShader(sp), nullptr);
    EXPECT_NE(ns::graphics::detail::GetPixelShader(sp), nullptr);
    EXPECT_NE(ns::graphics::detail::GetInputLayout(sp), nullptr);
}
