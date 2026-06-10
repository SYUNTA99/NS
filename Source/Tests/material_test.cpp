#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Material.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Graphics/Shader.h>
#include <Framework/Graphics/Texture.h>
#include <Framework/Platform/Window.h>

#include <memory>

namespace
{
    using NS::Graphics::Material;
    using NS::Graphics::MaterialDesc;
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::Shader;
    using NS::Graphics::Texture;
    using NS::Graphics::TextureDesc;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    // 存在しない .vs. / .ps. パスで magenta fallback shader になる (ステージはファイル名で判定)
    constexpr const char* kFallbackVsPath = "C:/nonexistent/__ns_mat_fallback.vs.hlsl";
    constexpr const char* kFallbackPsPath = "C:/nonexistent/__ns_mat_fallback.ps.hlsl";

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

    std::unique_ptr<Shader> vsHolder = Shader::Create(kFallbackVsPath);
    Shader& vs = *vsHolder;
    std::unique_ptr<Shader> psHolder = Shader::Create(kFallbackPsPath);
    Shader& ps = *psHolder;
    ASSERT_TRUE(vs.IsValid());
    ASSERT_TRUE(ps.IsValid());

    MaterialDesc desc{};
    desc.vertexShader = &vs;
    desc.pixelShader = &ps;
    desc.constantBufferSize = sizeof(DummyCB);
    desc.cbSlot = 1;

    std::unique_ptr<Material> matHolder = Material::Create(desc);
    Material& mat = *matHolder;
    EXPECT_TRUE(mat.IsValid());
}

TEST_F(MaterialLoggerTest, ConstructWithoutShaderIsInvalid)
{
    Window window(MakeWindowDesc("ns_mat_no_shader"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    MaterialDesc desc{};
    desc.vertexShader = nullptr;
    desc.pixelShader = nullptr;
    desc.constantBufferSize = sizeof(DummyCB);

    std::unique_ptr<Material> matHolder = Material::Create(desc);
    Material& mat = *matHolder;
    EXPECT_FALSE(mat.IsValid());
}

TEST_F(MaterialLoggerTest, ConstructWithZeroCbSizeStillValid)
{
    Window window(MakeWindowDesc("ns_mat_no_cb"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::unique_ptr<Shader> vsHolder = Shader::Create(kFallbackVsPath);
    Shader& vs = *vsHolder;
    std::unique_ptr<Shader> psHolder = Shader::Create(kFallbackPsPath);
    Shader& ps = *psHolder;
    ASSERT_TRUE(vs.IsValid());
    ASSERT_TRUE(ps.IsValid());

    MaterialDesc desc{};
    desc.vertexShader = &vs;
    desc.pixelShader = &ps;
    desc.constantBufferSize = 0u;

    std::unique_ptr<Material> matHolder = Material::Create(desc);
    Material& mat = *matHolder;
    EXPECT_TRUE(mat.IsValid());
}

TEST_F(MaterialLoggerTest, SetAndClearTextureBindDoesNotCrash)
{
    Window window(MakeWindowDesc("ns_mat_texture"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::unique_ptr<Shader> vsHolder = Shader::Create(kFallbackVsPath);
    Shader& vs = *vsHolder;
    std::unique_ptr<Shader> psHolder = Shader::Create(kFallbackPsPath);
    Shader& ps = *psHolder;
    ASSERT_TRUE(vs.IsValid());
    ASSERT_TRUE(ps.IsValid());

    std::unique_ptr<Texture> textureHolder = Texture::Create(TextureDesc{});
    Texture& texture = *textureHolder;
    ASSERT_TRUE(texture.IsValid());

    MaterialDesc desc{};
    desc.vertexShader = &vs;
    desc.pixelShader = &ps;
    desc.constantBufferSize = sizeof(DummyCB);
    std::unique_ptr<Material> matHolder = Material::Create(desc);
    Material& mat = *matHolder;
    ASSERT_TRUE(mat.IsValid());

    // SetTexture / ClearTexture / 未割当 slot に ClearTexture の各 call path が crash しないこと
    // 内部状態を query する公開 API は意図的に持たない (Deep Module 化、 raw pointer 露出回避)
    mat.SetTexture(0u, &texture);
    mat.Bind(renderer);
    mat.ClearTexture(0u);
    mat.ClearTexture(1u);
    mat.Bind(renderer);
    SUCCEED();
}

TEST_F(MaterialLoggerTest, BindDoesNotCrash)
{
    Window window(MakeWindowDesc("ns_mat_bind"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::unique_ptr<Shader> vsHolder = Shader::Create(kFallbackVsPath);
    Shader& vs = *vsHolder;
    std::unique_ptr<Shader> psHolder = Shader::Create(kFallbackPsPath);
    Shader& ps = *psHolder;
    ASSERT_TRUE(vs.IsValid());
    ASSERT_TRUE(ps.IsValid());

    std::unique_ptr<Texture> textureHolder = Texture::Create(TextureDesc{});
    Texture& texture = *textureHolder;
    ASSERT_TRUE(texture.IsValid());

    MaterialDesc desc{};
    desc.vertexShader = &vs;
    desc.pixelShader = &ps;
    desc.constantBufferSize = sizeof(DummyCB);
    std::unique_ptr<Material> matHolder = Material::Create(desc);
    Material& mat = *matHolder;
    ASSERT_TRUE(mat.IsValid());

    mat.SetTexture(0u, &texture);
    DummyCB cb{};
    cb.mvp[0] = 1.0f;
    cb.mvp[5] = 1.0f;
    cb.mvp[10] = 1.0f;
    cb.mvp[15] = 1.0f;
    mat.SetParams(renderer, cb);

    mat.Bind(renderer);
    SUCCEED();
}

TEST_F(MaterialLoggerTest, InvalidMaterialBindIsNoOp)
{
    Window window(MakeWindowDesc("ns_mat_invalid_bind"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    MaterialDesc desc{};
    desc.vertexShader = nullptr;
    desc.pixelShader = nullptr;
    std::unique_ptr<Material> matHolder = Material::Create(desc);
    Material& mat = *matHolder;
    ASSERT_FALSE(mat.IsValid());

    mat.Bind(renderer);
    SUCCEED();
}

TEST_F(MaterialLoggerTest, SetParamsWithoutCbIsNoOp)
{
    Window window(MakeWindowDesc("ns_mat_params_no_cb"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::unique_ptr<Shader> vsHolder = Shader::Create(kFallbackVsPath);
    Shader& vs = *vsHolder;
    std::unique_ptr<Shader> psHolder = Shader::Create(kFallbackPsPath);
    Shader& ps = *psHolder;
    ASSERT_TRUE(vs.IsValid());
    ASSERT_TRUE(ps.IsValid());

    MaterialDesc desc{};
    desc.vertexShader = &vs;
    desc.pixelShader = &ps;
    desc.constantBufferSize = 0u;
    std::unique_ptr<Material> matHolder = Material::Create(desc);
    Material& mat = *matHolder;
    ASSERT_TRUE(mat.IsValid());

    DummyCB cb{};
    mat.SetParams(renderer, cb);
    mat.Bind(renderer);
    SUCCEED();
}

TEST_F(MaterialLoggerTest, SetParamsBeforeBindNoCrash)
{
    Window window(MakeWindowDesc("ns_mat_params"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::unique_ptr<Shader> vsHolder = Shader::Create(kFallbackVsPath);
    Shader& vs = *vsHolder;
    std::unique_ptr<Shader> psHolder = Shader::Create(kFallbackPsPath);
    Shader& ps = *psHolder;
    ASSERT_TRUE(vs.IsValid());
    ASSERT_TRUE(ps.IsValid());

    MaterialDesc desc{};
    desc.vertexShader = &vs;
    desc.pixelShader = &ps;
    desc.constantBufferSize = sizeof(DummyCB);
    std::unique_ptr<Material> matHolder = Material::Create(desc);
    Material& mat = *matHolder;
    ASSERT_TRUE(mat.IsValid());

    DummyCB cb{};
    mat.SetParams(renderer, cb);
    SUCCEED();
}
