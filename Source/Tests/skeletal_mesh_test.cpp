#include <gtest/gtest.h>

#include <Framework/Graphics/Mesh.h>
#include <Framework/Graphics/SkeletalMesh.h>

#include <cstddef>
#include <type_traits>

TEST(SkeletalMeshShellTest, DerivesFromMeshAndIsInvalid)
{
    static_assert(std::is_base_of<NS::Graphics::Mesh, NS::Graphics::SkeletalMesh>::value,
                  "SkeletalMesh は Mesh を基底に持つ");

    NS::Graphics::SkeletalMesh sm;
    EXPECT_FALSE(sm.IsValid());
    EXPECT_EQ(sm.VertexCount(), std::size_t{0});
    EXPECT_EQ(sm.IndexCount(), std::size_t{0});
    sm.Draw();
    SUCCEED();
}

TEST(SkinnedVertexLayoutTest, SizeIs64)
{
    EXPECT_EQ(sizeof(NS::Graphics::SkinnedVertex), 64u);
}

TEST(SkinnedVertexLayoutTest, IsStandardLayout)
{
    EXPECT_TRUE(std::is_standard_layout_v<NS::Graphics::SkinnedVertex>);
}

TEST(SkinnedVertexLayoutTest, MemberOffsetsMatchGpuStride)
{
    using NS::Graphics::SkinnedVertex;
    EXPECT_EQ(offsetof(SkinnedVertex, position), 0u);
    EXPECT_EQ(offsetof(SkinnedVertex, uv), 12u);
    EXPECT_EQ(offsetof(SkinnedVertex, normal), 20u);
    EXPECT_EQ(offsetof(SkinnedVertex, joints), 32u);
    EXPECT_EQ(offsetof(SkinnedVertex, weights), 48u);
}
