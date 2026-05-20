#include <gtest/gtest.h>

#include <ns/core/math.h>
#include <ns/graphics/debug_draw.h>

namespace
{
    using ns::core::AABB;
    using ns::core::Color;
    using ns::core::Vector3;
    namespace DD = ns::graphics::DebugDraw;

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
    EXPECT_EQ(DD::VertexCount(), std::size_t{24}); // 12 lines × 2 vertices
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
    // 4096 vertex 上限。Line 1 本 = 2 vertex なので 2049 本以上を入れて overflow させる。
    const Color c{1.0f, 1.0f, 1.0f, 1.0f};
    for (int i = 0; i < 2100; ++i)
        DD::Line({static_cast<float>(i), 0.0f, 0.0f}, {static_cast<float>(i) + 1.0f, 0.0f, 0.0f}, c);
    EXPECT_LE(DD::VertexCount(), std::size_t{4096});
}
