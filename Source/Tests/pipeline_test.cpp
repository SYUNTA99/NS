#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/CommandList.h>
#include <Framework/Graphics/Pipeline.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Platform/Window.h>

namespace
{
    using NS::Graphics::BlendMode;
    using NS::Graphics::CullMode;
    using NS::Graphics::DepthMode;
    using NS::Graphics::Pipeline;
    using NS::Graphics::PipelineDesc;
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
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
        d.enableDebugLayer = false;
        d.vsync = false;
        return d;
    }
} // namespace

class PipelineTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(PipelineTest, CreateDefaultDescIsValid)
{
    Window window(MakeWindowDesc("ns_pipeline_default"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto pipeline = Pipeline::Create(PipelineDesc{});
    ASSERT_NE(pipeline, nullptr);
    EXPECT_TRUE(pipeline->IsValid());
    EXPECT_NE(pipeline->RasterizerState(), nullptr);
    EXPECT_NE(pipeline->BlendState(), nullptr);
    EXPECT_NE(pipeline->DepthStencilState(), nullptr);
}

TEST_F(PipelineTest, DescRoundTripKeepsIntent)
{
    Window window(MakeWindowDesc("ns_pipeline_desc"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    PipelineDesc desc{};
    desc.cull = CullMode::None;
    desc.blend = BlendMode::Alpha;
    desc.depth = DepthMode::ReadOnly;
    const auto pipeline = Pipeline::Create(desc);
    ASSERT_TRUE(pipeline->IsValid());

    EXPECT_EQ(pipeline->Desc().cull, CullMode::None);
    EXPECT_EQ(pipeline->Desc().blend, BlendMode::Alpha);
    EXPECT_EQ(pipeline->Desc().depth, DepthMode::ReadOnly);
}

TEST_F(PipelineTest, CreateWithoutDeviceIsInvalid)
{
    // Renderer を作らない = Gpu().device が空。失敗しても非 null + IsValid()==false の規約を検証する
    const auto pipeline = Pipeline::Create(PipelineDesc{});
    ASSERT_NE(pipeline, nullptr);
    EXPECT_FALSE(pipeline->IsValid());
}

TEST_F(PipelineTest, SetPipelineDoesNotCrash)
{
    Window window(MakeWindowDesc("ns_pipeline_set"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto pipeline = Pipeline::Create(PipelineDesc{});
    ASSERT_TRUE(pipeline->IsValid());

    renderer.BeginFrame();
    renderer.Commands().SetPipeline(*pipeline);
    renderer.EndFrame();
    SUCCEED();
}
