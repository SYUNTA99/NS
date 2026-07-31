#include <gtest/gtest.h>

#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/Pipeline.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Graphics/Skybox.h>
#include <Runtime/Platform/Window.h>


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

    ASSERT_NE(skybox.RenderPipeline(), nullptr);
    const NS::Graphics::PipelineDesc& desc = skybox.RenderPipeline()->Desc();

    // skybox は z=1 の far plane に張り付くので LESS_EQUAL + 書込なし (= ReadOnly) 必須
    EXPECT_EQ(desc.depth, NS::Graphics::DepthMode::ReadOnly);
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

    ASSERT_NE(skybox.RenderPipeline(), nullptr);
    const NS::Graphics::PipelineDesc& desc = skybox.RenderPipeline()->Desc();

    EXPECT_EQ(desc.fill, NS::Graphics::FillMode::Solid);
    // inside-out cube を視点中心で描くので Front or None が許容される
    const bool cullOk = (desc.cull == NS::Graphics::CullMode::Front) || (desc.cull == NS::Graphics::CullMode::None);
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

    // LoadCubemap を呼ばずに発行しても fallback が描かれてクラッシュしないこと
    NS::Math::Matrix vpNoTranslate = NS::Math::Matrix::Identity;
    NS::Graphics::IssueSkybox(renderer, skybox, vpNoTranslate);
    SUCCEED();
}
