#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Buffer.h>
#include <Framework/Graphics/CommandList.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Graphics/ShaderStage.h>
#include <Framework/Platform/Window.h>

#include <array>
#include <cstdint>
#include <memory>

namespace
{
    using NS::Graphics::Buffer;
    using NS::Graphics::BufferDesc;
    using NS::Graphics::MakeConstantBufferDesc;
    using NS::Graphics::MakeIndexBufferDesc;
    using NS::Graphics::MakeVertexBufferDesc;
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::ShaderStage;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    struct alignas(16) TestCB
    {
        float values[4];
    };
    static_assert(sizeof(TestCB) == 16, "TestCB は 16 byte");

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

class BufferLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST_F(BufferLoggerTest, VertexBufferStaticConstructs)
{
    Window window(MakeWindowDesc("ns_vb_static"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const std::array<TestVertex, 3> verts{
        TestVertex{{0.0f, 1.0f, 0.0f}, {0.5f, 0.0f}},
        TestVertex{{-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
        TestVertex{{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
    };

    BufferDesc vbDesc = MakeVertexBufferDesc(verts.data(), verts.size(), sizeof(TestVertex));
    std::unique_ptr<Buffer> vbHolder = Buffer::Create(vbDesc);
    Buffer& vb = *vbHolder;
    EXPECT_TRUE(vb.IsValid());
    EXPECT_EQ(vb.Stride(), sizeof(TestVertex));
    EXPECT_EQ(vb.ByteSize(), verts.size() * sizeof(TestVertex));
}

TEST_F(BufferLoggerTest, VertexBufferDynamicUpdateDoesNotCrash)
{
    Window window(MakeWindowDesc("ns_vb_dynamic"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::array<TestVertex, 3> verts{};
    BufferDesc vbDesc = MakeVertexBufferDesc(verts.data(), verts.size(), sizeof(TestVertex), D3D11_USAGE_DYNAMIC);
    std::unique_ptr<Buffer> vbHolder = Buffer::Create(vbDesc);
    Buffer& vb = *vbHolder;
    ASSERT_TRUE(vb.IsValid());

    verts[0].pos[0] = 5.0f;
    renderer.Commands().UpdateBuffer(vb, verts.data(), verts.size() * sizeof(TestVertex));
    SUCCEED();
}

TEST_F(BufferLoggerTest, VertexBufferStaticUpdateIsNoop)
{
    Window window(MakeWindowDesc("ns_vb_static_update"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::array<TestVertex, 3> verts{};
    BufferDesc vbDesc = MakeVertexBufferDesc(verts.data(), verts.size(), sizeof(TestVertex));
    std::unique_ptr<Buffer> vbHolder = Buffer::Create(vbDesc);
    Buffer& vb = *vbHolder;
    ASSERT_TRUE(vb.IsValid());

    renderer.Commands().UpdateBuffer(vb, verts.data(), verts.size() * sizeof(TestVertex));
    SUCCEED();
}

TEST_F(BufferLoggerTest, IndexBufferUint16Constructs)
{
    Window window(MakeWindowDesc("ns_ib_u16"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
    BufferDesc ibDesc = MakeIndexBufferDesc(indices.data(), indices.size(), DXGI_FORMAT_R16_UINT);
    std::unique_ptr<Buffer> ibHolder = Buffer::Create(ibDesc);
    Buffer& ib = *ibHolder;
    EXPECT_TRUE(ib.IsValid());
    EXPECT_EQ(ib.Format(), DXGI_FORMAT_R16_UINT);
    EXPECT_EQ(ib.ByteSize(), indices.size() * sizeof(std::uint16_t));
}

TEST_F(BufferLoggerTest, IndexBufferUint32Constructs)
{
    Window window(MakeWindowDesc("ns_ib_u32"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    BufferDesc ibDesc = MakeIndexBufferDesc(indices.data(), indices.size(), DXGI_FORMAT_R32_UINT);
    std::unique_ptr<Buffer> ibHolder = Buffer::Create(ibDesc);
    Buffer& ib = *ibHolder;
    EXPECT_TRUE(ib.IsValid());
    EXPECT_EQ(ib.Format(), DXGI_FORMAT_R32_UINT);
    EXPECT_EQ(ib.ByteSize(), indices.size() * sizeof(std::uint32_t));
}

TEST_F(BufferLoggerTest, ConstantBufferConstructs)
{
    Window window(MakeWindowDesc("ns_cb_construct"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    BufferDesc cbDesc = MakeConstantBufferDesc(64);
    std::unique_ptr<Buffer> cbHolder = Buffer::Create(cbDesc);
    Buffer& cb = *cbHolder;
    EXPECT_TRUE(cb.IsValid());
    EXPECT_EQ(cb.ByteSize(), static_cast<std::size_t>(64));
}

TEST_F(BufferLoggerTest, ConstantBufferRoundsUpTo16ByteBoundary)
{
    Window window(MakeWindowDesc("ns_cb_round"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    BufferDesc cbDesc = MakeConstantBufferDesc(20);
    std::unique_ptr<Buffer> cbHolder = Buffer::Create(cbDesc);
    Buffer& cb = *cbHolder;
    EXPECT_TRUE(cb.IsValid());
    EXPECT_EQ(cb.ByteSize(), static_cast<std::size_t>(32));
}

TEST_F(BufferLoggerTest, ConstantBufferUpdate)
{
    Window window(MakeWindowDesc("ns_cb_update"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    BufferDesc cbDesc = MakeConstantBufferDesc(sizeof(TestCB));
    std::unique_ptr<Buffer> cbHolder = Buffer::Create(cbDesc);
    Buffer& cb = *cbHolder;
    ASSERT_TRUE(cb.IsValid());

    TestCB data{};
    data.values[0] = 1.0f;
    data.values[1] = 2.0f;
    data.values[2] = 3.0f;
    data.values[3] = 4.0f;
    renderer.Commands().UpdateBuffer(cb, &data, sizeof(data));
    SUCCEED();
}

TEST_F(BufferLoggerTest, BufferBindsDoNotCrash)
{
    Window window(MakeWindowDesc("ns_buffer_bind"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const std::array<TestVertex, 3> verts{};
    BufferDesc vbDesc = MakeVertexBufferDesc(verts.data(), verts.size(), sizeof(TestVertex));
    std::unique_ptr<Buffer> vbHolder = Buffer::Create(vbDesc);
    Buffer& vb = *vbHolder;
    ASSERT_TRUE(vb.IsValid());

    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    BufferDesc ibDesc = MakeIndexBufferDesc(indices.data(), indices.size(), DXGI_FORMAT_R16_UINT);
    std::unique_ptr<Buffer> ibHolder = Buffer::Create(ibDesc);
    Buffer& ib = *ibHolder;
    ASSERT_TRUE(ib.IsValid());

    BufferDesc cbDesc = MakeConstantBufferDesc(sizeof(TestCB));
    std::unique_ptr<Buffer> cbHolder = Buffer::Create(cbDesc);
    Buffer& cb = *cbHolder;
    ASSERT_TRUE(cb.IsValid());

    renderer.Commands().SetVertexBuffer(vb, 0);
    renderer.Commands().SetIndexBuffer(ib);
    renderer.Commands().SetConstantBuffer(cb, 0, ShaderStage::Vertex | ShaderStage::Pixel);
    SUCCEED();
}
