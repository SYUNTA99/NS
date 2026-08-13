#include <Runtime/Core/Logger.h>
#include <Runtime/Graphics/Buffer.h>
#include <Runtime/Graphics/CommandList.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Graphics/Shader.h>
#include <Runtime/Graphics/Texture.h>
#include <Runtime/Platform/Window.h>
#include <array>
#include <cstdint>
#include <gtest/gtest.h>
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
    cmd.VSSetShader(vs);
    cmd.PSSetShader(ps);
    cmd.SetInputLayout(nullptr); // nullptr は何もしない。実レイアウト経路は mesh テストが見る
    cmd.SetVertexBuffer(vb, 0);
    cmd.SetIndexBuffer(ib);
    cmd.VSSetConstantBuffer(cb, 0);
    cmd.PSSetConstantBuffer(cb, 0);
    cmd.PSSetShaderResource(tex, 0);
    cmd.DrawIndexed(3);
    SUCCEED();
}

TEST_F(CommandListLoggerTest, UpdateHandlesDynamicAndDefaultBuffers)
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
    cmd.UpdateSubresource(dyn, verts.data(), verts.size() * sizeof(TestVertex));
    // DEFAULT は UpdateSubresource で更新されて落ちない
    cmd.UpdateSubresource(stat, verts.data(), verts.size() * sizeof(TestVertex));
    SUCCEED();
}

TEST_F(CommandListLoggerTest, UpdateTextureDefaultUsesUpdateSubresource)
{
    Window window(MakeWindowDesc("ns_cmd_tex_update"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    TextureCreateDesc texDesc{};
    texDesc.width = 2;
    texDesc.height = 2;
    texDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.bindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::unique_ptr<Texture> texHolder = Texture::Create(texDesc);
    Texture& tex = *texHolder;
    ASSERT_TRUE(tex.IsValid());

    // DEFAULT texture は UpdateSubresource 経路。rowPitch = width * 4 byte
    const std::array<unsigned int, 4> pixels{0xFFFFFFFFu, 0xFF000000u, 0xFF000000u, 0xFFFFFFFFu};
    renderer.Commands().UpdateSubresource(tex, pixels.data(), 2u * sizeof(unsigned int));
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
