#include <Runtime/Core/Math.h>
#include <Runtime/Graphics/DebugDraw.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Color;
    using NS::Core::Vector3;
    namespace DD = NS::Graphics::DebugDraw;

    void Reset() noexcept
    {
        DD::Clear();
    }
} // namespace

TEST(DebugDrawTest, LineAdds2Vertices)
{
    Reset();
    DD::Line({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f});
    EXPECT_EQ(DD::VertexCount(), std::size_t{2});
}

TEST(DebugDrawTest, AABBAdds24Vertices)
{
    Reset();
    AABB box;
    box.Center = {0.0f, 0.0f, 0.0f};
    box.Extents = {1.0f, 1.0f, 1.0f};
    DD::AABB(box, Color{1.0f, 0.0f, 0.0f, 1.0f});
    EXPECT_EQ(DD::VertexCount(), std::size_t{24}); // 辺 12 本 × 2 頂点
}

TEST(DebugDrawTest, ObbAdds24Vertices)
{
    Reset();
    DD::OBB(NS::Core::OBB{Vector3{0.0f, 0.0f, 0.0f},
                          Vector3{1.0f, 0.0f, 0.0f},
                          Vector3{0.0f, 1.0f, 0.0f},
                          Vector3{0.0f, 0.0f, 1.0f},
                          1.0f,
                          1.0f,
                          1.0f},
            Color{0.0f, 1.0f, 0.0f, 1.0f});
    EXPECT_EQ(DD::VertexCount(), std::size_t{24}); // 辺 12 本 × 2 頂点
}

TEST(DebugDrawTest, SphereAdds3GreatCircles)
{
    Reset();
    DD::Sphere(NS::Core::Sphere{Vector3{0.0f, 0.0f, 0.0f}, 1.0f}, Color{0.0f, 1.0f, 0.0f, 1.0f});
    EXPECT_EQ(DD::VertexCount(), std::size_t{72}); // 大円 3 本 × 12 分割 × 2 頂点
}

TEST(DebugDrawTest, ClearResetsToZero)
{
    DD::Line({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f});
    EXPECT_GT(DD::VertexCount(), std::size_t{0});
    DD::Clear();
    EXPECT_EQ(DD::VertexCount(), std::size_t{0});
}

TEST(DebugDrawTest, CapsuleAccumulatesNonZeroVertices)
{
    Reset();
    DD::Capsule({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.5f, Color{0.0f, 1.0f, 0.0f, 1.0f});
    EXPECT_GT(DD::VertexCount(), std::size_t{0});
}

TEST(DebugDrawTest, OverflowDropsOldestSilentlyAndCapsAtMaximum)
{
    Reset();
    // 4096 頂点が上限。Line 1 本 = 2 頂点なので 2049 本以上入れて溢れさせる
    const Color c{1.0f, 1.0f, 1.0f, 1.0f};
    for (int i = 0; i < 2100; ++i)
        DD::Line({static_cast<float>(i), 0.0f, 0.0f}, {static_cast<float>(i) + 1.0f, 0.0f, 0.0f}, c);
    EXPECT_LE(DD::VertexCount(), std::size_t{4096});
}
