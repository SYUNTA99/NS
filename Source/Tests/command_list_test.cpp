#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Buffer.h>
#include <Framework/Graphics/CommandList.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Graphics/Shader.h>
#include <Framework/Graphics/Texture.h>
#include <Framework/Platform/Window.h>

#include <array>
#include <cstdint>

namespace
{
    using NS::Graphics::Buffer;
    using NS::Graphics::BufferUsage;
    using NS::Graphics::CommandList;
    using NS::Graphics::IndexFormat;
    using NS::Graphics::MakeConstantBufferDesc;
    using NS::Graphics::MakeIndexBufferDesc;
    using NS::Graphics::MakeVertexBufferDesc;
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::Shader;
    using NS::Graphics::ShaderStage;
    using NS::Graphics::Texture;
    using NS::Graphics::TextureCreateDesc;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    struct TestVertex
    {
        float pos[3];
        float uv[2];
    };

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

class CommandListLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(CommandListLoggerTest, CommandsAccessibleFromRenderer)
{
    Window window(MakeWindowDesc("ns_cmd_access"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    EXPECT_NE(renderer.Commands().Native(), nullptr);
}

TEST_F(CommandListLoggerTest, SetAndDrawDoNotCrash)
{
    Window window(MakeWindowDesc("ns_cmd_setdraw"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const std::array<TestVertex, 3> verts{};
    Buffer vb(renderer, MakeVertexBufferDesc(verts.data(), verts.size(), sizeof(TestVertex)));
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    Buffer ib(renderer, MakeIndexBufferDesc(indices.data(), indices.size(), IndexFormat::UInt16));
    Buffer cb(renderer, MakeConstantBufferDesc(64));
    ASSERT_TRUE(vb.IsValid());
    ASSERT_TRUE(ib.IsValid());
    ASSERT_TRUE(cb.IsValid());

    // 読込失敗で magenta fallback になり IsValid()==true
    Shader vs(renderer, "C:/nonexistent/__ns_cmd.vs.hlsl");
    Shader ps(renderer, "C:/nonexistent/__ns_cmd.ps.hlsl");
    ASSERT_TRUE(vs.IsValid());
    ASSERT_TRUE(ps.IsValid());
    Texture tex(renderer, "C:/nonexistent/__ns_cmd.png");
    ASSERT_TRUE(tex.IsValid());

    CommandList& cmd = renderer.Commands();
    cmd.SetShader(vs);
    cmd.SetShader(ps);
    cmd.SetVertexBuffer(vb, 0);
    cmd.SetIndexBuffer(ib);
    cmd.SetConstantBuffer(cb, 0, ShaderStage::Vertex | ShaderStage::Pixel);
    cmd.SetTexture(tex, 0, ShaderStage::Pixel);
    cmd.DrawIndexed(3);
    SUCCEED();
}

TEST_F(CommandListLoggerTest, UpdateBufferDynamicWorksStaticIsNoop)
{
    Window window(MakeWindowDesc("ns_cmd_update"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::array<TestVertex, 3> verts{};
    Buffer dyn(renderer, MakeVertexBufferDesc(verts.data(), verts.size(), sizeof(TestVertex), BufferUsage::Dynamic));
    Buffer stat(renderer, MakeVertexBufferDesc(verts.data(), verts.size(), sizeof(TestVertex)));
    ASSERT_TRUE(dyn.IsValid());
    ASSERT_TRUE(stat.IsValid());

    CommandList& cmd = renderer.Commands();
    verts[0].pos[0] = 1.0f;
    cmd.UpdateBuffer(dyn, verts.data(), verts.size() * sizeof(TestVertex));
    // Static への Update は no-op (ERROR ログのみ、 crash しない)
    cmd.UpdateBuffer(stat, verts.data(), verts.size() * sizeof(TestVertex));
    SUCCEED();
}

TEST_F(CommandListLoggerTest, RenderTargetAndClearDoNotCrash)
{
    Window window(MakeWindowDesc("ns_cmd_rt"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    TextureCreateDesc colorDesc{};
    colorDesc.width = 64;
    colorDesc.height = 64;
    colorDesc.bindFlags = D3D11_BIND_RENDER_TARGET;
    Texture color(renderer, colorDesc);

    TextureCreateDesc depthDesc{};
    depthDesc.width = 64;
    depthDesc.height = 64;
    depthDesc.format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.bindFlags = D3D11_BIND_DEPTH_STENCIL;
    Texture depth(renderer, depthDesc);

    ASSERT_TRUE(color.IsValid());
    ASSERT_TRUE(depth.IsValid());

    CommandList& cmd = renderer.Commands();
    cmd.ClearRenderTarget(color.Rtv(), 0.1f, 0.2f, 0.3f, 1.0f);
    cmd.ClearDepth(depth.Dsv(), 1.0f);
    cmd.SetRenderTarget(color.Rtv(), depth.Dsv());
    cmd.SetViewport(64.0f, 64.0f);

    // backbuffer を描画先に張り直しても crash しない
    renderer.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    SUCCEED();
}
