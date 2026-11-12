#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Geometry/SkinnedBounds.h"
#include "NSlib/Core/Geometry/Vertex.h"
#include "NSlib/Core/Math.h"

#include <gtest/gtest.h>

#include <vector>

namespace
{
    NS::SkinnedVertex VertexOfBone(NS::uint32 bone, const NS::Vector3& position)
    {
        NS::SkinnedVertex vertex{};
        vertex.position = position;
        vertex.joints[0] = bone;
        vertex.weights[0] = 1.0f;
        return vertex;
    }
} // namespace

// 影響の球は、骨に重みのある頂点の範囲の中心と、そこから一番遠い頂点までの距離。重みの無い骨は負の半径
TEST(SkinnedBounds, SpheresWrapTheVerticesEachBoneMoves)
{
    const std::vector<NS::SkinnedVertex> vertices = {VertexOfBone(0, NS::Vector3{0.0f, 0.0f, 0.0f}),
                                                     VertexOfBone(0, NS::Vector3{2.0f, 0.0f, 0.0f})};
    const std::vector<NS::BoneSphere> spheres = NS::ComputeBoneSpheres(vertices.data(), vertices.size(), 2u);

    ASSERT_EQ(spheres.size(), 2u);
    EXPECT_FLOAT_EQ(spheres[0].center.x, 1.0f);
    EXPECT_FLOAT_EQ(spheres[0].radius, 1.0f);
    EXPECT_LT(spheres[1].radius, 0.0f);
}

// 今の姿勢の境界は、骨の行列で動かした球を包む箱。球が 1 つも無ければ代わりの箱
TEST(SkinnedBounds, MergedBoundsFollowThePaletteOrFallBack)
{
    const std::vector<NS::BoneSphere> spheres = {NS::BoneSphere{NS::Vector3{1.0f, 0.0f, 0.0f}, 1.0f},
                                                 NS::BoneSphere{NS::Vector3{0.0f, 0.0f, 0.0f}, -1.0f}};
    const NS::Matrix palette[2] = {NS::Matrix::CreateTranslation(0.0f, 3.0f, 0.0f), NS::Matrix::Identity};
    const NS::AABB fallback{NS::Vector3{9.0f, 9.0f, 9.0f}, NS::Vector3{1.0f, 1.0f, 1.0f}};

    const NS::AABB merged = NS::MergeSkinnedBounds(spheres, palette, 2u, fallback);
    EXPECT_FLOAT_EQ(merged.Center.x, 1.0f);
    EXPECT_FLOAT_EQ(merged.Center.y, 3.0f);
    EXPECT_FLOAT_EQ(merged.Extents.x, 1.0f);

    const std::vector<NS::BoneSphere> none = {NS::BoneSphere{NS::Vector3{0.0f, 0.0f, 0.0f}, -1.0f}};
    const NS::AABB kept = NS::MergeSkinnedBounds(none, palette, 1u, fallback);
    EXPECT_FLOAT_EQ(kept.Center.x, 9.0f);
}
