#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Buffer.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Platform/Window.h>

#include <array>
#include <cstdint>

namespace
{
    using NS::Graphics::BufferUsage;
    using NS::Graphics::ConstantBuffer;
    using NS::Graphics::HasStage;
    using NS::Graphics::IndexBuffer;
    using NS::Graphics::IndexBufferDesc;
    using NS::Graphics::IndexFormat;
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::ShaderStage;
    using NS::Graphics::VertexBuffer;
    using NS::Graphics::VertexBufferDesc;
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

class BufferLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST(NsGraphicsShaderStage, BitflagOperators)
{
    constexpr auto vp = ShaderStage::Vertex | ShaderStage::Pixel;
    EXPECT_TRUE(HasStage(vp, ShaderStage::Vertex));
    EXPECT_TRUE(HasStage(vp, ShaderStage::Pixel));
    EXPECT_FALSE(HasStage(vp, ShaderStage::Geometry));

    EXPECT_TRUE(HasStage(ShaderStage::All, ShaderStage::Vertex));
    EXPECT_TRUE(HasStage(ShaderStage::All, ShaderStage::Pixel));
    EXPECT_TRUE(HasStage(ShaderStage::All, ShaderStage::Geometry));
    EXPECT_FALSE(HasStage(ShaderStage::None, ShaderStage::Vertex));
}

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

    VertexBufferDesc desc{};
    desc.initialData = verts.data();
    desc.vertexCount = verts.size();
    desc.stride = sizeof(TestVertex);
    desc.usage = BufferUsage::Static;

    VertexBuffer vb(renderer, desc);
    EXPECT_TRUE(vb.IsValid());
    EXPECT_EQ(vb.Stride(), sizeof(TestVertex));
    EXPECT_EQ(vb.VertexCount(), verts.size());
}

TEST_F(BufferLoggerTest, VertexBufferDynamicUpdateDoesNotCrash)
{
    Window window(MakeWindowDesc("ns_vb_dynamic"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::array<TestVertex, 3> verts{};
    VertexBufferDesc desc{};
    desc.initialData = verts.data();
    desc.vertexCount = verts.size();
    desc.stride = sizeof(TestVertex);
    desc.usage = BufferUsage::Dynamic;

    VertexBuffer vb(renderer, desc);
    ASSERT_TRUE(vb.IsValid());

    verts[0].pos[0] = 5.0f;
    vb.UpdateRaw(verts.data(), verts.size() * sizeof(TestVertex));
    SUCCEED();
}

TEST_F(BufferLoggerTest, VertexBufferStaticUpdateIsNoop)
{
    Window window(MakeWindowDesc("ns_vb_static_update"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    std::array<TestVertex, 3> verts{};
    VertexBufferDesc desc{};
    desc.initialData = verts.data();
    desc.vertexCount = verts.size();
    desc.stride = sizeof(TestVertex);
    desc.usage = BufferUsage::Static;

    VertexBuffer vb(renderer, desc);
    ASSERT_TRUE(vb.IsValid());

    vb.UpdateRaw(verts.data(), verts.size() * sizeof(TestVertex));
    SUCCEED();
}

TEST_F(BufferLoggerTest, IndexBufferUint16Constructs)
{
    Window window(MakeWindowDesc("ns_ib_u16"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
    IndexBufferDesc desc{};
    desc.initialData = indices.data();
    desc.indexCount = indices.size();
    desc.format = IndexFormat::UInt16;
    desc.usage = BufferUsage::Static;

    IndexBuffer ib(renderer, desc);
    EXPECT_TRUE(ib.IsValid());
    EXPECT_EQ(ib.Format(), IndexFormat::UInt16);
    EXPECT_EQ(ib.IndexCount(), indices.size());
}

TEST_F(BufferLoggerTest, IndexBufferUint32Constructs)
{
    Window window(MakeWindowDesc("ns_ib_u32"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    IndexBufferDesc desc{};
    desc.initialData = indices.data();
    desc.indexCount = indices.size();
    desc.format = IndexFormat::UInt32;
    desc.usage = BufferUsage::Static;

    IndexBuffer ib(renderer, desc);
    EXPECT_TRUE(ib.IsValid());
    EXPECT_EQ(ib.Format(), IndexFormat::UInt32);
    EXPECT_EQ(ib.IndexCount(), indices.size());
}

TEST_F(BufferLoggerTest, ConstantBufferConstructs)
{
    Window window(MakeWindowDesc("ns_cb_construct"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    ConstantBuffer cb(renderer, 64);
    EXPECT_TRUE(cb.IsValid());
    EXPECT_EQ(cb.ByteSize(), static_cast<std::size_t>(64));
}

TEST_F(BufferLoggerTest, ConstantBufferRoundsUpTo16ByteBoundary)
{
    Window window(MakeWindowDesc("ns_cb_round"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    ConstantBuffer cb(renderer, 20);
    EXPECT_TRUE(cb.IsValid());
    EXPECT_EQ(cb.ByteSize(), static_cast<std::size_t>(32));
}

TEST_F(BufferLoggerTest, ConstantBufferUpdate)
{
    Window window(MakeWindowDesc("ns_cb_update"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    ConstantBuffer cb(renderer, sizeof(TestCB));
    ASSERT_TRUE(cb.IsValid());

    TestCB data{};
    data.values[0] = 1.0f;
    data.values[1] = 2.0f;
    data.values[2] = 3.0f;
    data.values[3] = 4.0f;
    cb.Update(data);
    SUCCEED();
}

TEST_F(BufferLoggerTest, BufferBindsDoNotCrash)
{
    Window window(MakeWindowDesc("ns_buffer_bind"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const std::array<TestVertex, 3> verts{};
    VertexBufferDesc vbDesc{};
    vbDesc.initialData = verts.data();
    vbDesc.vertexCount = verts.size();
    vbDesc.stride = sizeof(TestVertex);
    VertexBuffer vb(renderer, vbDesc);
    ASSERT_TRUE(vb.IsValid());

    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    IndexBufferDesc ibDesc{};
    ibDesc.initialData = indices.data();
    ibDesc.indexCount = indices.size();
    ibDesc.format = IndexFormat::UInt16;
    IndexBuffer ib(renderer, ibDesc);
    ASSERT_TRUE(ib.IsValid());

    ConstantBuffer cb(renderer, sizeof(TestCB));
    ASSERT_TRUE(cb.IsValid());

    vb.Bind(0);
    ib.Bind();
    cb.Bind(0, ShaderStage::Vertex | ShaderStage::Pixel);
    SUCCEED();
}
