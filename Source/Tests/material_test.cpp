#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Material.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Graphics/ShaderProgram.h>
#include <Framework/Graphics/Texture.h>
#include <Framework/Platform/Window.h>

namespace
{
    using NS::Graphics::InputElement;
    using NS::Graphics::InputElementFormat;
    using NS::Graphics::Material;
    using NS::Graphics::MaterialDesc;
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::ShaderProgram;
    using NS::Graphics::ShaderProgramDesc;
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
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
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
