#include <gtest/gtest.h>

#include <Framework/Core/Logger.h>
#include <Framework/Graphics/Mesh.h>
#include <Framework/Graphics/Renderer.h>
#include <Framework/Graphics/SkeletalMesh.h>
#include <Framework/Math/Math.h>
#include <Framework/Platform/Window.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace
{
    using NS::Graphics::InputElement;
    using NS::Graphics::InputElementFormat;
    using NS::Graphics::Renderer;
    using NS::Graphics::RendererDesc;
    using NS::Graphics::SkeletalMesh;
    using NS::Graphics::SkinnedMeshDesc;
    using NS::Graphics::SkinnedVertex;
    using NS::Math::Matrix;
    using NS::Math::Vector2;
    using NS::Math::Vector3;
    using NS::Platform::Window;
    using NS::Platform::WindowDesc;

    static_assert(std::is_base_of_v<NS::Graphics::Mesh, SkeletalMesh>, "SkeletalMesh は Mesh を基底に持つ");

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

    constexpr std::size_t kTriVertexCount = 3;
    constexpr std::size_t kTriIndexCount = 3;

    std::array<SkinnedVertex, kTriVertexCount> MakeSkinnedTriangle()
    {
        std::array<SkinnedVertex, kTriVertexCount> v{};
        for (SkinnedVertex& vertex : v)
        {
            vertex.normal = Vector3(0.0f, 0.0f, -1.0f);
            vertex.joints[0] = 0;
            vertex.weights[0] = 1.0f;
        }
        v[0].position = Vector3(-1.0f, -1.0f, 0.0f);
        v[0].uv = Vector2(0.0f, 1.0f);
        v[1].position = Vector3(0.0f, 1.0f, 0.0f);
        v[1].uv = Vector2(0.5f, 0.0f);
        v[2].position = Vector3(1.0f, -1.0f, 0.0f);
        v[2].uv = Vector2(1.0f, 1.0f);
        return v;
    }

    std::array<std::uint32_t, kTriIndexCount> MakeTriangleIndices()
    {
        return {0, 1, 2};
    }
} // namespace

class SkeletalMeshLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST(SkinnedVertexLayoutTest, SizeIs64)
{
    EXPECT_EQ(sizeof(SkinnedVertex), 64u);
}

TEST(SkinnedVertexLayoutTest, IsStandardLayout)
{
    EXPECT_TRUE(std::is_standard_layout_v<SkinnedVertex>);
}

TEST(SkinnedVertexLayoutTest, MemberOffsetsMatchGpuStride)
{
    EXPECT_EQ(offsetof(SkinnedVertex, position), 0u);
    EXPECT_EQ(offsetof(SkinnedVertex, uv), 12u);
    EXPECT_EQ(offsetof(SkinnedVertex, normal), 20u);
    EXPECT_EQ(offsetof(SkinnedVertex, joints), 32u);
    EXPECT_EQ(offsetof(SkinnedVertex, weights), 48u);
}

TEST(SkeletalMeshInputLayoutTest, SkinnedInputLayoutHasExpectedElements)
{
    const auto elements = SkeletalMesh::SkinnedInputLayout();
    ASSERT_EQ(elements.size(), 5u);

    EXPECT_EQ(elements[0].semanticName, "POSITION");
    EXPECT_EQ(elements[0].format, InputElementFormat::Float3);
    EXPECT_EQ(elements[0].byteOffset, 0u);

    EXPECT_EQ(elements[1].semanticName, "TEXCOORD");
    EXPECT_EQ(elements[1].format, InputElementFormat::Float2);
    EXPECT_EQ(elements[1].byteOffset, 12u);

    EXPECT_EQ(elements[2].semanticName, "NORMAL");
    EXPECT_EQ(elements[2].format, InputElementFormat::Float3);
    EXPECT_EQ(elements[2].byteOffset, 20u);

    EXPECT_EQ(elements[3].semanticName, "BLENDINDICES");
    EXPECT_EQ(elements[3].format, InputElementFormat::UInt4);
    EXPECT_EQ(elements[3].byteOffset, 32u);

    EXPECT_EQ(elements[4].semanticName, "BLENDWEIGHT");
    EXPECT_EQ(elements[4].format, InputElementFormat::Float4);
    EXPECT_EQ(elements[4].byteOffset, 48u);
}

TEST_F(SkeletalMeshLoggerTest, ConstructWithValidDescIsValid)
{
    Window window(MakeWindowDesc("ns_skeletal_valid"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto vertices = MakeSkinnedTriangle();
    const auto indices = MakeTriangleIndices();
    SkinnedMeshDesc desc{};
    desc.vertices = vertices.data();
    desc.vertexCount = vertices.size();
    desc.indices = indices.data();
    desc.indexCount = indices.size();
    desc.boneCount = 2;

    SkeletalMesh mesh(renderer, desc);
    EXPECT_TRUE(mesh.IsValid());
    EXPECT_FALSE(mesh.IsUsingFallback());
    EXPECT_EQ(mesh.VertexCount(), kTriVertexCount);
    EXPECT_EQ(mesh.IndexCount(), kTriIndexCount);
    EXPECT_EQ(mesh.BoneCount(), 2u);
}

TEST_F(SkeletalMeshLoggerTest, EmptyDescIsInvalidWithoutFallback)
{
    Window window(MakeWindowDesc("ns_skeletal_empty"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    SkinnedMeshDesc desc{};
    SkeletalMesh mesh(renderer, desc);
    EXPECT_FALSE(mesh.IsValid());
    EXPECT_FALSE(mesh.IsUsingFallback());
    EXPECT_EQ(mesh.VertexCount(), 0u);
    EXPECT_EQ(mesh.IndexCount(), 0u);

    // 不正な mesh の Draw は no-op でクラッシュしない
    mesh.Draw();
    SUCCEED();
}

TEST_F(SkeletalMeshLoggerTest, SetBonePaletteAndDrawDoesNotCrash)
{
    Window window(MakeWindowDesc("ns_skeletal_draw"));
    ASSERT_TRUE(window.IsValid());
    Renderer renderer(MakeRendererDesc(), window);
    ASSERT_TRUE(renderer.IsValid());

    const auto vertices = MakeSkinnedTriangle();
    const auto indices = MakeTriangleIndices();
    SkinnedMeshDesc desc{};
    desc.vertices = vertices.data();
    desc.vertexCount = vertices.size();
    desc.indices = indices.data();
    desc.indexCount = indices.size();
    desc.boneCount = 2;

    SkeletalMesh mesh(renderer, desc);
    ASSERT_TRUE(mesh.IsValid());

    const std::array<Matrix, 2> palette{Matrix::Identity, Matrix::Identity};
    mesh.SetBonePalette(std::span<const Matrix>(palette.data(), palette.size()));
    mesh.Draw();
    SUCCEED();
}
