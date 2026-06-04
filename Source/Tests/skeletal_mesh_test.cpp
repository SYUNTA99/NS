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
