#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Graphics/StaticMesh.h>
#include <Framework/Platform/Window.h>

#include <array>
#include <cstdint>

namespace
{
    using NS::Math::Vector2;
    using NS::Math::Vector3;
    using NS::Graphics::InputElement;
    using NS::Graphics::InputElementFormat;
    using NS::Graphics::StaticMesh;
    using NS::Graphics::MeshDesc;
    using NS::Graphics::StaticVertex;
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
        d.vsync = false;
        d.enableDebugLayer = false;
        return d;
    }

    constexpr std::size_t kCubeVertexCount = 8;
    constexpr std::size_t kCubeIndexCount = 36;

    std::array<StaticVertex, kCubeVertexCount> MakeCubeVertices()
    {
        std::array<StaticVertex, kCubeVertexCount> v{};
        v[0] = {Vector3(-1.0f, -1.0f, -1.0f), Vector2(0.0f, 0.0f), Vector3(0.0f, 0.0f, -1.0f)};
        v[1] = {Vector3(1.0f, -1.0f, -1.0f), Vector2(1.0f, 0.0f), Vector3(0.0f, 0.0f, -1.0f)};
        v[2] = {Vector3(1.0f, 1.0f, -1.0f), Vector2(1.0f, 1.0f), Vector3(0.0f, 0.0f, -1.0f)};
        v[3] = {Vector3(-1.0f, 1.0f, -1.0f), Vector2(0.0f, 1.0f), Vector3(0.0f, 0.0f, -1.0f)};
        v[4] = {Vector3(-1.0f, -1.0f, 1.0f), Vector2(0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)};
        v[5] = {Vector3(1.0f, -1.0f, 1.0f), Vector2(1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)};
        v[6] = {Vector3(1.0f, 1.0f, 1.0f), Vector2(1.0f, 1.0f), Vector3(0.0f, 0.0f, 1.0f)};
        v[7] = {Vector3(-1.0f, 1.0f, 1.0f), Vector2(0.0f, 1.0f), Vector3(0.0f, 0.0f, 1.0f)};
        return v;
    }

    std::array<std::uint32_t, kCubeIndexCount> MakeCubeIndices()
    {
        return {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 4, 5, 1, 4, 1, 0,
                3, 2, 6, 3, 6, 7, 1, 5, 6, 1, 6, 2, 4, 0, 3, 4, 3, 7};
    }
} // namespace

class MeshLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST(MeshTypeLayoutTest, StaticVertexSizeIs32)
{
    EXPECT_EQ(sizeof(StaticVertex), 32u);
}

TEST(MeshTypeLayoutTest, StandardInputLayoutHasExpectedElements)
{
    const auto elements = StaticMesh::StandardInputLayout();
    ASSERT_EQ(elements.size(), 3u);

    EXPECT_EQ(elements[0].semanticName, "POSITION");
    EXPECT_EQ(elements[0].format, InputElementFormat::Float3);
    EXPECT_EQ(elements[0].byteOffset, 0u);

    EXPECT_EQ(elements[1].semanticName, "TEXCOORD");
    EXPECT_EQ(elements[1].format, InputElementFormat::Float2);
    EXPECT_EQ(elements[1].byteOffset, 12u);

    EXPECT_EQ(elements[2].semanticName, "NORMAL");
    EXPECT_EQ(elements[2].format, InputElementFormat::Float3);
    EXPECT_EQ(elements[2].byteOffset, 20u);
}

TEST_F(MeshLoggerTest, ConstructWithCubeDataIsValid)
{
    Window window(MakeWindowDesc("ns_mesh_cube"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto vertices = MakeCubeVertices();
    const auto indices = MakeCubeIndices();
    MeshDesc desc{};
    desc.vertices = vertices.data();
    desc.vertexCount = vertices.size();
    desc.indices = indices.data();
    desc.indexCount = indices.size();

    StaticMesh mesh(renderer, desc);
    EXPECT_TRUE(mesh.IsValid());
    EXPECT_EQ(mesh.VertexCount(), kCubeVertexCount);
    EXPECT_EQ(mesh.IndexCount(), kCubeIndexCount);
}

TEST_F(MeshLoggerTest, EmptyDescFallsBackToCube)
{
    Window window(MakeWindowDesc("ns_mesh_empty"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    MeshDesc desc{};
    StaticMesh mesh(renderer, desc);
    EXPECT_TRUE(mesh.IsValid());
    EXPECT_TRUE(mesh.IsUsingFallback());
    EXPECT_GT(mesh.VertexCount(), 0u);
    EXPECT_GT(mesh.IndexCount(), 0u);
}

TEST_F(MeshLoggerTest, PartiallyEmptyDescFallsBackToCube)
{
    Window window(MakeWindowDesc("ns_mesh_partial"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto vertices = MakeCubeVertices();
    const auto indices = MakeCubeIndices();

    // vertices あり / indices nullptr
    {
        MeshDesc desc{};
        desc.vertices = vertices.data();
        desc.vertexCount = vertices.size();
        desc.indices = nullptr;
        desc.indexCount = indices.size();
        StaticMesh mesh(renderer, desc);
        EXPECT_TRUE(mesh.IsValid());
        EXPECT_TRUE(mesh.IsUsingFallback());
    }

    // indices あり / vertexCount=0
    {
        MeshDesc desc{};
        desc.vertices = vertices.data();
        desc.vertexCount = 0u;
        desc.indices = indices.data();
        desc.indexCount = indices.size();
        StaticMesh mesh(renderer, desc);
        EXPECT_TRUE(mesh.IsValid());
        EXPECT_TRUE(mesh.IsUsingFallback());
    }
}

TEST_F(MeshLoggerTest, FallbackMeshDrawDoesNotCrash)
{
    Window window(MakeWindowDesc("ns_mesh_invalid_draw"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    MeshDesc desc{};
    StaticMesh mesh(renderer, desc);
    ASSERT_TRUE(mesh.IsValid());
    ASSERT_TRUE(mesh.IsUsingFallback());

    mesh.Draw();
    SUCCEED();
}

TEST_F(MeshLoggerTest, DrawDoesNotCrash)
{
    Window window(MakeWindowDesc("ns_mesh_draw"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto vertices = MakeCubeVertices();
    const auto indices = MakeCubeIndices();
    MeshDesc desc{};
    desc.vertices = vertices.data();
    desc.vertexCount = vertices.size();
    desc.indices = indices.data();
    desc.indexCount = indices.size();

    StaticMesh mesh(renderer, desc);
    ASSERT_TRUE(mesh.IsValid());

    mesh.Draw();
    SUCCEED();
}

TEST_F(MeshLoggerTest, AccessorsNonNull)
{
    Window window(MakeWindowDesc("ns_mesh_accessors"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto vertices = MakeCubeVertices();
    const auto indices = MakeCubeIndices();
    MeshDesc desc{};
    desc.vertices = vertices.data();
    desc.vertexCount = vertices.size();
    desc.indices = indices.data();
    desc.indexCount = indices.size();

    StaticMesh mesh(renderer, desc);
    ASSERT_TRUE(mesh.IsValid());

    EXPECT_NE(NS::Graphics::detail::GetVertexBuffer(mesh), nullptr);
    EXPECT_NE(NS::Graphics::detail::GetIndexBuffer(mesh), nullptr);
}
