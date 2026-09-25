#include <Runtime/Core/Math.h>
#include <Runtime/Graphics/MeshPrimitives.h>
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>

namespace
{
    using NS::Core::Vector2;
    using NS::Core::Vector3;
    using NS::Gfx::MakeCapsule;
    using NS::Gfx::MakeCube;
    using NS::Gfx::MakePlane;

    bool IsAxisAligned(const Vector3& n) noexcept
    {
        const float ax = std::abs(n.x);
        const float ay = std::abs(n.y);
        const float az = std::abs(n.z);
        const float maxAxis = std::max({ax, ay, az});
        const float sum = ax + ay + az;
        return std::abs(maxAxis - 1.0f) < 1e-3f && std::abs(sum - 1.0f) < 1e-3f;
    }

    // 当たりのカプセルと同じ定義で、軸の線分 (0, ±halfHeight, 0) 上の最も近い点
    Vector3 ClosestOnCapsuleAxis(const Vector3& p, float halfHeight) noexcept
    {
        return Vector3{0.0f, std::clamp(p.y, -halfHeight, halfHeight), 0.0f};
    }

    // 頂点が全部カプセルの表面に乗り、法線が単位長で表面の外を向き、三角形の表が外を向くかを見る
    void ExpectCapsuleSurface(const NS::Gfx::MeshGeometry& geom, float radius, float halfHeight)
    {
        ASSERT_FALSE(geom.vertices.empty());
        ASSERT_EQ(geom.indices.size() % 3, std::size_t{0});

        float maxX = 0.0f;
        float maxZ = 0.0f;
        float maxY = -1.0f;
        float minY = 1.0f;
        for (const NS::Gfx::StaticVertex& v : geom.vertices)
        {
            const Vector3 offset = v.position - ClosestOnCapsuleAxis(v.position, halfHeight);
            EXPECT_NEAR(offset.Length(), radius, 1e-5f) << "軸の線分から半径の距離に無い頂点がある";
            EXPECT_NEAR(v.normal.Length(), 1.0f, 1e-5f);
            EXPECT_NEAR(v.normal.Dot(offset / radius), 1.0f, 1e-4f) << "法線が表面の外を向いていない";
            maxX = std::max(maxX, std::abs(v.position.x));
            maxZ = std::max(maxZ, std::abs(v.position.z));
            maxY = std::max(maxY, v.position.y);
            minY = std::min(minY, v.position.y);
        }
        EXPECT_NEAR(maxX, radius, 1e-5f);
        EXPECT_NEAR(maxZ, radius, 1e-5f);
        EXPECT_NEAR(maxY, halfHeight + radius, 1e-5f);
        EXPECT_NEAR(minY, -(halfHeight + radius), 1e-5f);

        // 描画の並びのまま (v1 - v0) × (v2 - v0) が外を向く。極と赤道の継ぎ目の面積 0
        // の三角形は向きを持たないので飛ばす
        for (std::size_t i = 0; i < geom.indices.size(); i += 3)
        {
            ASSERT_LT(geom.indices[i], geom.vertices.size());
            ASSERT_LT(geom.indices[i + 1], geom.vertices.size());
            ASSERT_LT(geom.indices[i + 2], geom.vertices.size());
            const Vector3& a = geom.vertices[geom.indices[i]].position;
            const Vector3& b = geom.vertices[geom.indices[i + 1]].position;
            const Vector3& c = geom.vertices[geom.indices[i + 2]].position;
            const Vector3 face = (b - a).Cross(c - a);
            if (face.Length() < 1e-7f)
            {
                continue;
            }
            const Vector3 center = (a + b + c) / 3.0f;
            EXPECT_GT(face.Dot(center - ClosestOnCapsuleAxis(center, halfHeight)), 0.0f)
                << "裏を向いた三角形 " << i / 3;
        }
    }
} // namespace

TEST(MeshPrimitivesTest, MakeCubeHas24VerticesAnd36Indices)
{
    NS::Gfx::MeshGeometry geom = MakeCube({0.5f, 0.5f, 0.5f});
    EXPECT_EQ(geom.vertices.size(), std::size_t{24});
    EXPECT_EQ(geom.indices.size(), std::size_t{36});
}

TEST(MeshPrimitivesTest, MakeCubeNormalsAreAxisAligned)
{
    NS::Gfx::MeshGeometry geom = MakeCube({0.5f, 0.5f, 0.5f});
    for (const NS::Gfx::StaticVertex& v : geom.vertices)
        EXPECT_TRUE(IsAxisAligned(v.normal)) << "normal should be axis-aligned (per-face)";
}

TEST(MeshPrimitivesTest, MakeCubeRespectsExtents)
{
    NS::Gfx::MeshGeometry geom = MakeCube({2.0f, 3.0f, 4.0f});
    float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
    for (const NS::Gfx::StaticVertex& v : geom.vertices)
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
    NS::Gfx::MeshGeometry geom = MakePlane({1.0f, 1.0f});
    EXPECT_EQ(geom.vertices.size(), std::size_t{4});
    EXPECT_EQ(geom.indices.size(), std::size_t{6});
}

// 自機の立ち姿は当たりのカプセルと同じ寸法で描く。半分の高さは当たりと同じく円柱部の半長で、上端は半長 + 半径
TEST(MeshPrimitivesTest, MakeCapsuleLiesOnTheCollisionCapsuleSurface)
{
    ExpectCapsuleSurface(MakeCapsule(0.65f, 0.5f), 0.65f, 0.5f);
}

// 自機の玉は円柱の長さ 0 のカプセル。同じ半径の球になる
TEST(MeshPrimitivesTest, MakeCapsuleWithZeroHalfHeightIsASphere)
{
    ExpectCapsuleSurface(MakeCapsule(0.4f, 0.0f), 0.4f, 0.0f);
}

TEST(MeshPrimitivesTest, MakePlaneNormalsArePlusY)
{
    NS::Gfx::MeshGeometry geom = MakePlane({2.5f, 3.5f});
    for (const NS::Gfx::StaticVertex& v : geom.vertices)
    {
        EXPECT_FLOAT_EQ(v.normal.x, 0.0f);
        EXPECT_FLOAT_EQ(v.normal.y, 1.0f);
        EXPECT_FLOAT_EQ(v.normal.z, 0.0f);
        EXPECT_FLOAT_EQ(v.position.y, 0.0f);
    }
}
