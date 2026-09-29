#include <Runtime/Core/AABB.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Core/OBB.h>
#include <Runtime/Core/Sphere.h>
#include <Runtime/Graphics/DebugDraw.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Color;
    using NS::Core::Vector3;
    namespace DD = NS::Gfx::DebugDraw;

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

TEST(DebugDrawTest, CircleAdds12Segments)
{
    Reset();
    DD::Circle({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f});
    EXPECT_EQ(DD::VertexCount(), std::size_t{24}); // 12 分割 × 2 頂点
}

TEST(DebugDrawTest, ClearResetsToZero)
{
    DD::Line({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f});
    EXPECT_GT(DD::VertexCount(), std::size_t{0});
    DD::Clear();
    EXPECT_EQ(DD::VertexCount(), std::size_t{0});
}

// 両端の半球も弧で描く。上下の円と側面の線だけだと円柱に見える
TEST(DebugDrawTest, CapsuleDrawsBothHemispheres)
{
    Reset();
    DD::Capsule({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.5f, Color{0.0f, 1.0f, 0.0f, 1.0f});
    // 上下の円 2 本 × 12 分割 × 2 頂点 + 側面の線 4 本 × 2 頂点 + 半球の弧 4 本 × 6 分割 × 2 頂点
    EXPECT_EQ(DD::VertexCount(), std::size_t{104});
}

TEST(DebugDrawTest, CapsuleAccumulatesNonZeroVertices)
{
    Reset();
    DD::Capsule({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.5f, Color{0.0f, 1.0f, 0.0f, 1.0f});
    EXPECT_GT(DD::VertexCount(), std::size_t{0});
}

// 面は線と別に積む。線の数は変えない
TEST(DebugDrawTest, TriangleAdds3FaceVerticesApartFromLines)
{
    Reset();
    DD::Triangle({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, Color{1.0f, 0.0f, 0.0f, 0.2f});
    EXPECT_EQ(DD::FaceVertexCount(), std::size_t{3});
    EXPECT_EQ(DD::VertexCount(), std::size_t{0});
}

TEST(DebugDrawTest, ClearDropsFacesToo)
{
    Reset();
    DD::Triangle({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, Color{1.0f, 0.0f, 0.0f, 0.2f});
    DD::Clear();
    EXPECT_EQ(DD::FaceVertexCount(), std::size_t{0});
}

// 面は上限の 12960 枚までは 1 枚も捨てない
TEST(DebugDrawTest, FacesHoldUpToTheirCapacity)
{
    Reset();
    const Color c{1.0f, 1.0f, 1.0f, 0.2f};
    constexpr int k_Triangles = 10 * 27 * 24 * 2;
    for (int i = 0; i < k_Triangles; ++i)
    {
        const float x = static_cast<float>(i);
        DD::Triangle({x, 0.0f, 0.0f}, {x + 1.0f, 0.0f, 0.0f}, {x, 1.0f, 0.0f}, c);
    }
    EXPECT_EQ(DD::FaceVertexCount(), static_cast<std::size_t>(k_Triangles) * 3);
}

// 面も上限を超えたら古い物から捨て、上限の 3 の倍数で止まる
TEST(DebugDrawTest, FaceOverflowDropsOldestAndCapsAtMaximum)
{
    Reset();
    const Color c{1.0f, 1.0f, 1.0f, 0.2f};
    for (int i = 0; i < 15000; ++i)
    {
        const float x = static_cast<float>(i);
        DD::Triangle({x, 0.0f, 0.0f}, {x + 1.0f, 0.0f, 0.0f}, {x, 1.0f, 0.0f}, c);
    }
    EXPECT_LE(DD::FaceVertexCount(), std::size_t{38880});
    EXPECT_EQ(DD::FaceVertexCount() % 3, std::size_t{0});
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
