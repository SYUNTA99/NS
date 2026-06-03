#include <gtest/gtest.h>

#include <Framework/Math/Math.h>
#include <Framework/Graphics/MeshPrimitives.h>

#include <algorithm>
#include <cmath>

namespace
{
    using NS::Math::Vector2;
    using NS::Math::Vector3;
    using NS::Graphics::MakeCube;
    using NS::Graphics::MakePlane;

    bool IsAxisAligned(const Vector3& n) noexcept
    {
        const float ax = std::abs(n.x);
        const float ay = std::abs(n.y);
        const float az = std::abs(n.z);
        const float maxAxis = std::max({ax, ay, az});
        const float sum = ax + ay + az;
        return std::abs(maxAxis - 1.0f) < 1e-3f && std::abs(sum - 1.0f) < 1e-3f;
    }
} // namespace

TEST(MeshPrimitivesTest, MakeCubeHas24VerticesAnd36Indices)
{
    auto geom = MakeCube({0.5f, 0.5f, 0.5f});
    EXPECT_EQ(geom.vertices.size(), std::size_t{24});
    EXPECT_EQ(geom.indices.size(), std::size_t{36});
}

TEST(MeshPrimitivesTest, MakeCubeNormalsAreAxisAligned)
{
    auto geom = MakeCube({0.5f, 0.5f, 0.5f});
    for (const auto& v : geom.vertices)
        EXPECT_TRUE(IsAxisAligned(v.normal)) << "normal should be axis-aligned (per-face)";
}

TEST(MeshPrimitivesTest, MakeCubeRespectsExtents)
{
    auto geom = MakeCube({2.0f, 3.0f, 4.0f});
    float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
    for (const auto& v : geom.vertices)
    {
        maxX = std::max(maxX, std::abs(v.position.x));
        maxY = std::max(maxY, std::abs(v.position.y));
        maxZ = std::max(maxZ, std::abs(v.position.z));
    }
    EXPECT_FLOAT_EQ(maxX, 2.0f);
    EXPECT_FLOAT_EQ(maxY, 3.0f);
    EXPECT_FLOAT_EQ(maxZ, 4.0f);
}

TEST(MeshPrimitivesTest, MakePlaneHas4VerticesAnd6Indices)
{
    auto geom = MakePlane({1.0f, 1.0f});
    EXPECT_EQ(geom.vertices.size(), std::size_t{4});
    EXPECT_EQ(geom.indices.size(), std::size_t{6});
}

TEST(MeshPrimitivesTest, MakePlaneNormalsArePlusY)
{
    auto geom = MakePlane({2.5f, 3.5f});
    for (const auto& v : geom.vertices)
    {
        EXPECT_FLOAT_EQ(v.normal.x, 0.0f);
        EXPECT_FLOAT_EQ(v.normal.y, 1.0f);
        EXPECT_FLOAT_EQ(v.normal.z, 0.0f);
        EXPECT_FLOAT_EQ(v.position.y, 0.0f);
    }
}
