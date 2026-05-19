#include <gtest/gtest.h>

#include <ns/core/logger.h>
#include <ns/graphics/material.h>
#include <ns/graphics/renderer.h>
#include <ns/graphics/shader_program.h>
#include <ns/graphics/texture.h>
#include <ns/platform/window.h>

namespace
{
    using ns::graphics::InputElement;
    using ns::graphics::InputElementFormat;
    using ns::graphics::Material;
    using ns::graphics::MaterialDesc;
    using ns::graphics::Renderer;
    using ns::graphics::RendererDesc;
    using ns::graphics::ShaderProgram;
    using ns::graphics::ShaderProgramDesc;
    using ns::graphics::ShaderStage;
    using ns::graphics::Texture;
    using ns::graphics::TextureDesc;
    using ns::platform::Window;
    using ns::platform::WindowDesc;

    WindowDesc MakeWindowDesc(const char* title)
    {
        WindowDesc d{};
        d.title = title;
        d.width = 320;
        d.height = 240;
        return d;
    }

    RendererDesc MakeRendererDesc()
    {
        RendererDesc d{};
        d.vsync = false;
        d.enableDebugLayer = false;
        return d;
    }

    ShaderProgramDesc MakeFallbackShaderDesc()
    {
        ShaderProgramDesc d{};
        d.inputLayout = {InputElement{"POSITION", InputElementFormat::Float3, 0u}};
        return d;
    }

    struct alignas(16) DummyCB
    {
        float mvp[16];
    };
    static_assert(sizeof(DummyCB) == 64);
} // namespace

class MaterialLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { ns::core::Logger::Init(); }
    void TearDown() override { ns::core::Logger::Shutdown(); }
};

TEST_F(MaterialLoggerTest, ConstructWithShaderIsValid)
{
    Window window(MakeWindowDesc("ns_mat_valid"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    ShaderProgram shader(renderer, MakeFallbackShaderDesc());
    ASSERT_TRUE(shader.IsValid());

    MaterialDesc desc{};
    desc.shader = &shader;
    desc.constantBufferSize = sizeof(DummyCB);
    desc.cbSlot = 1;
    desc.cbStages = ShaderStage::Vertex | ShaderStage::Pixel;

    Material mat(renderer, desc);
    EXPECT_TRUE(mat.IsValid());
    EXPECT_EQ(mat.Shader(), &shader);
}

TEST_F(MaterialLoggerTest, ConstructWithoutShaderIsInvalid)
{
    Window window(MakeWindowDesc("ns_mat_no_shader"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    MaterialDesc desc{};
    desc.shader = nullptr;
    desc.constantBufferSize = sizeof(DummyCB);

    Material mat(renderer, desc);
    EXPECT_FALSE(mat.IsValid());
    EXPECT_EQ(mat.Shader(), nullptr);
}

TEST_F(MaterialLoggerTest, ConstructWithZeroCbSizeStillValid)
{
    Window window(MakeWindowDesc("ns_mat_no_cb"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    ShaderProgram shader(renderer, MakeFallbackShaderDesc());
    ASSERT_TRUE(shader.IsValid());

    MaterialDesc desc{};
    desc.shader = &shader;
    desc.constantBufferSize = 0u;

    Material mat(renderer, desc);
    EXPECT_TRUE(mat.IsValid());
}

TEST_F(MaterialLoggerTest, SetTextureRetrievableViaGetter)
{
    Window window(MakeWindowDesc("ns_mat_texture"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    ShaderProgram shader(renderer, MakeFallbackShaderDesc());
    ASSERT_TRUE(shader.IsValid());

    Texture texture(renderer, TextureDesc{});
    ASSERT_TRUE(texture.IsValid());

    MaterialDesc desc{};
    desc.shader = &shader;
    desc.constantBufferSize = sizeof(DummyCB);
    Material mat(renderer, desc);
    ASSERT_TRUE(mat.IsValid());

    mat.SetTexture(0u, &texture);
    EXPECT_EQ(mat.GetTexture(0u), &texture);
    EXPECT_EQ(mat.GetTexture(1u), nullptr);

    mat.ClearTexture(0u);
    EXPECT_EQ(mat.GetTexture(0u), nullptr);
}

TEST_F(MaterialLoggerTest, BindDoesNotCrash)
{
    Window window(MakeWindowDesc("ns_mat_bind"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    ShaderProgram shader(renderer, MakeFallbackShaderDesc());
    ASSERT_TRUE(shader.IsValid());

    Texture texture(renderer, TextureDesc{});
    ASSERT_TRUE(texture.IsValid());

    MaterialDesc desc{};
    desc.shader = &shader;
    desc.constantBufferSize = sizeof(DummyCB);
    Material mat(renderer, desc);
    ASSERT_TRUE(mat.IsValid());

    mat.SetTexture(0u, &texture);
    DummyCB cb{};
    cb.mvp[0] = 1.0f;
    cb.mvp[5] = 1.0f;
    cb.mvp[10] = 1.0f;
    cb.mvp[15] = 1.0f;
    mat.SetParams(cb);

    mat.Bind();
    SUCCEED();
}

TEST_F(MaterialLoggerTest, InvalidMaterialBindIsNoOp)
{
    Window window(MakeWindowDesc("ns_mat_invalid_bind"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    MaterialDesc desc{};
    desc.shader = nullptr;
    Material mat(renderer, desc);
    ASSERT_FALSE(mat.IsValid());

    mat.Bind();
    SUCCEED();
}

TEST_F(MaterialLoggerTest, SetParamsWithoutCbIsNoOp)
{
    Window window(MakeWindowDesc("ns_mat_params_no_cb"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    ShaderProgram shader(renderer, MakeFallbackShaderDesc());
    ASSERT_TRUE(shader.IsValid());

    MaterialDesc desc{};
    desc.shader = &shader;
    desc.constantBufferSize = 0u;
    Material mat(renderer, desc);
    ASSERT_TRUE(mat.IsValid());

    DummyCB cb{};
    mat.SetParams(cb);
    mat.Bind();
    SUCCEED();
}

TEST_F(MaterialLoggerTest, SetParamsBeforeBindNoCrash)
{
    Window window(MakeWindowDesc("ns_mat_params"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    ShaderProgram shader(renderer, MakeFallbackShaderDesc());
    ASSERT_TRUE(shader.IsValid());

    MaterialDesc desc{};
    desc.shader = &shader;
    desc.constantBufferSize = sizeof(DummyCB);
    Material mat(renderer, desc);
    ASSERT_TRUE(mat.IsValid());

    DummyCB cb{};
    mat.SetParams(cb);
    SUCCEED();
}
