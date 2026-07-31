#include <gtest/gtest.h>
#include <memory>
#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/Material.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Graphics/Shader.h>
#include <Runtime/Graphics/Texture.h>
#include <Runtime/Platform/Window.h>

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

    // ファイル名の .vs. / .ps. でステージを判定する。存在しないパスは magenta fallback shader になる
    constexpr const char* k_FallbackVsPath = "C:/nonexistent/__ns_mat_fallback.vs.hlsl";
    constexpr const char* k_FallbackPsPath = "C:/nonexistent/__ns_mat_fallback.ps.hlsl";

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

TEST_F(MaterialLoggerTest, BlendAndRenderPriorityReflectDescEvenWhenInvalid)
{
    // blend / renderPriority は GPU リソースに依らない分類メタなので、device が無い状態でも desc の値を返す
    MaterialDesc desc{};
    desc.blend = NS::Graphics::BlendMode::Alpha;
    desc.renderPriority = 7;
    const auto material = Material::Create(desc);
    ASSERT_NE(material, nullptr);
    EXPECT_FALSE(material->IsValid());
    EXPECT_EQ(material->Blend(), NS::Graphics::BlendMode::Alpha);
    EXPECT_EQ(material->RenderPriority(), 7);
}

TEST_F(MaterialLoggerTest, DefaultBlendIsOpaque)
{
    const auto material = Material::Create(MaterialDesc{});
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(material->Blend(), NS::Graphics::BlendMode::Opaque);
    EXPECT_EQ(material->RenderPriority(), 0);
}

TEST_F(MaterialLoggerTest, ConstructWithShaderIsValid)
{
    Window window(MakeWindowDesc("ns_mat_valid"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::unique_ptr<Shader> vsHolder = Shader::Create(k_FallbackVsPath);
    Shader& vs = *vsHolder;
    std::unique_ptr<Shader> psHolder = Shader::Create(k_FallbackPsPath);
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

    std::unique_ptr<Shader> vsHolder = Shader::Create(k_FallbackVsPath);
    Shader& vs = *vsHolder;
    std::unique_ptr<Shader> psHolder = Shader::Create(k_FallbackPsPath);
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

    std::unique_ptr<Shader> vsHolder = Shader::Create(k_FallbackVsPath);
    Shader& vs = *vsHolder;
    std::unique_ptr<Shader> psHolder = Shader::Create(k_FallbackPsPath);
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

    // SetTexture / ClearTexture / 未割当 slot への ClearTexture がどれも落ちないか確認
    // 内部状態を見る公開 API はあえて用意しない、raw pointer を外に出さないため
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

    std::unique_ptr<Shader> vsHolder = Shader::Create(k_FallbackVsPath);
    Shader& vs = *vsHolder;
    std::unique_ptr<Shader> psHolder = Shader::Create(k_FallbackPsPath);
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

    std::unique_ptr<Shader> vsHolder = Shader::Create(k_FallbackVsPath);
    Shader& vs = *vsHolder;
    std::unique_ptr<Shader> psHolder = Shader::Create(k_FallbackPsPath);
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

    std::unique_ptr<Shader> vsHolder = Shader::Create(k_FallbackVsPath);
    Shader& vs = *vsHolder;
    std::unique_ptr<Shader> psHolder = Shader::Create(k_FallbackPsPath);
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
