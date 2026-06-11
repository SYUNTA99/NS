#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Graphics/Skybox.h>
#include <Framework/Platform/Window.h>

#include <d3d11.h>

namespace
{
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::Skybox;
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

class SkyboxRenderingTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(SkyboxRenderingTest, DepthStateIsLessEqual)
{
    Window window(MakeWindowDesc("ns_skybox_depth"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::unique_ptr<Skybox> skyboxHolder = Skybox::Create();
    Skybox& skybox = *skyboxHolder;
    ASSERT_TRUE(skybox.IsValid());

    const D3D11_DEPTH_STENCIL_DESC desc = skybox.DepthStateDesc();

    EXPECT_TRUE(desc.DepthEnable);
    // skybox は z=1 の far plane に張り付くので LESS_EQUAL 必須
    EXPECT_EQ(desc.DepthFunc, D3D11_COMPARISON_LESS_EQUAL);
    // depth には書き込まない (後続透過オブジェクトのため)
    EXPECT_EQ(desc.DepthWriteMask, D3D11_DEPTH_WRITE_MASK_ZERO);
}

TEST_F(SkyboxRenderingTest, RasterFrontCull)
{
    Window window(MakeWindowDesc("ns_skybox_raster"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::unique_ptr<Skybox> skyboxHolder = Skybox::Create();
    Skybox& skybox = *skyboxHolder;
    ASSERT_TRUE(skybox.IsValid());

    const D3D11_RASTERIZER_DESC desc = skybox.RasterStateDesc();

    EXPECT_EQ(desc.FillMode, D3D11_FILL_SOLID);
    // inside-out cube を視点中心で描くので FRONT or NONE が許容される
    const bool cullOk = (desc.CullMode == D3D11_CULL_FRONT) || (desc.CullMode == D3D11_CULL_NONE);
    EXPECT_TRUE(cullOk);
}

TEST_F(SkyboxRenderingTest, RenderWithFallbackDoesNotCrash)
{
    Window window(MakeWindowDesc("ns_skybox_render_fallback"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::unique_ptr<Skybox> skyboxHolder = Skybox::Create();
    Skybox& skybox = *skyboxHolder;
    ASSERT_TRUE(skybox.IsValid());

    // LoadCubemap を呼ばずに Render() しても fallback が描かれてクラッシュしないこと
    NS::Math::Matrix vpNoTranslate = NS::Math::Matrix::Identity;
    skybox.Render(renderer, vpNoTranslate);
    SUCCEED();
}
