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
#include <memory>

namespace
{
    using NS::Graphics::Buffer;
    using NS::Graphics::BufferDesc;
    using NS::Graphics::CommandList;
    using NS::Graphics::MakeConstantBufferDesc;
    using NS::Graphics::MakeIndexBufferDesc;
    using NS::Graphics::MakeVertexBufferDesc;
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::Shader;
    using NS::Graphics::ShaderType;
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
    BufferDesc vbDesc = MakeVertexBufferDesc(verts.data(), verts.size(), sizeof(TestVertex));
    std::unique_ptr<Buffer> vbHolder = Buffer::Create(vbDesc);
    Buffer& vb = *vbHolder;
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    BufferDesc ibDesc = MakeIndexBufferDesc(indices.data(), indices.size(), DXGI_FORMAT_R16_UINT);
    std::unique_ptr<Buffer> ibHolder = Buffer::Create(ibDesc);
    Buffer& ib = *ibHolder;
    BufferDesc cbDesc = MakeConstantBufferDesc(64);
    std::unique_ptr<Buffer> cbHolder = Buffer::Create(cbDesc);
    Buffer& cb = *cbHolder;
    ASSERT_TRUE(vb.IsValid());
    ASSERT_TRUE(ib.IsValid());
    ASSERT_TRUE(cb.IsValid());

    // 読込失敗で magenta fallback になり IsValid()==true
    std::unique_ptr<Shader> vsHolder = Shader::Create("C:/nonexistent/__ns_cmd.vs.hlsl");
    Shader& vs = *vsHolder;
    std::unique_ptr<Shader> psHolder = Shader::Create("C:/nonexistent/__ns_cmd.ps.hlsl");
    Shader& ps = *psHolder;
    ASSERT_TRUE(vs.IsValid());
    ASSERT_TRUE(ps.IsValid());
    std::unique_ptr<Texture> texHolder = Texture::Create("C:/nonexistent/__ns_cmd.png");
    Texture& tex = *texHolder;
    ASSERT_TRUE(tex.IsValid());

    CommandList& cmd = renderer.Commands();
    cmd.SetShader(vs);
    cmd.SetShader(ps);
    cmd.SetInputLayout(nullptr); // nullptr は何もしない (実レイアウト経路は mesh テストがカバー)
    cmd.SetVertexBuffer(vb, 0);
    cmd.SetIndexBuffer(ib);
    cmd.SetConstantBuffer(cb, 0, ShaderType::Vertex);
    cmd.SetConstantBuffer(cb, 0, ShaderType::Pixel);
    cmd.SetTexture(tex, 0, ShaderType::Pixel);
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
    BufferDesc dynDesc = MakeVertexBufferDesc(verts.data(), verts.size(), sizeof(TestVertex), D3D11_USAGE_DYNAMIC);
    std::unique_ptr<Buffer> dynHolder = Buffer::Create(dynDesc);
    Buffer& dyn = *dynHolder;
    BufferDesc statDesc = MakeVertexBufferDesc(verts.data(), verts.size(), sizeof(TestVertex));
    std::unique_ptr<Buffer> statHolder = Buffer::Create(statDesc);
    Buffer& stat = *statHolder;
    ASSERT_TRUE(dyn.IsValid());
    ASSERT_TRUE(stat.IsValid());

    CommandList& cmd = renderer.Commands();
    verts[0].pos[0] = 1.0f;
    cmd.UpdateBuffer(dyn, verts.data(), verts.size() * sizeof(TestVertex));
    // Static への Update は何もしない (ERROR ログのみ、 crash しない)
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
    std::unique_ptr<Texture> colorHolder = Texture::Create(colorDesc);
    Texture& color = *colorHolder;

    TextureCreateDesc depthDesc{};
    depthDesc.width = 64;
    depthDesc.height = 64;
    depthDesc.format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.bindFlags = D3D11_BIND_DEPTH_STENCIL;
    std::unique_ptr<Texture> depthHolder = Texture::Create(depthDesc);
    Texture& depth = *depthHolder;

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
