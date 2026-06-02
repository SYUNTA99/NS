#include <gtest/gtest.h>

#include <Framework/Core/Math.h>
#include <Framework/Physics/Capsule.h>
#include <Framework/Physics/SweptAABB.h>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Physics::Capsule;
    using NS::Physics::SweptCapsuleVsAABB;

    Capsule MakeCapsule(const Vector3& center, float radius = 0.4f, float halfHeight = 0.5f)
    {
        Capsule c;
        c.center = center;
        c.axis = {0.0f, 1.0f, 0.0f};
        c.radius = radius;
        c.halfHeight = halfHeight;
        return c;
    }

    AABB MakeAABB(const Vector3& center, const Vector3& extents)
    {
        AABB box;
        box.Center = center;
        box.Extents = extents;
        return box;
    }
} // namespace

TEST(SweptCapsuleTest, NoHitWhenMotionPointsAway)
{
    Capsule c = MakeCapsule({0.0f, 5.0f, 0.0f});
    AABB box = MakeAABB({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f});
    Vector3 motion{0.0f, 10.0f, 0.0f}; // 上方向 (AABB から離れる)

    float toi = -1.0f;
    Vector3 normal{};
    const bool hit = SweptCapsuleVsAABB(c, motion, box, toi, normal);
    EXPECT_FALSE(hit);
    EXPECT_FLOAT_EQ(toi, 1.0f);
}

TEST(SweptCapsuleTest, HitWhenMovingDownOntoFloor)
{
    // Capsule (radius=0.4, halfHeight=0.5) center y=3 → 底端 y=2.1
    // 床 AABB (center y=0, extents y=0.5) の天面 y=0.5。差分 = 1.6m
    // motion y=-3.2 で確実に貫通する
    Capsule c = MakeCapsule({0.0f, 3.0f, 0.0f});
    AABB floor = MakeAABB({0.0f, 0.0f, 0.0f}, {5.0f, 0.5f, 5.0f});
    Vector3 motion{0.0f, -3.2f, 0.0f};

    float toi = -1.0f;
    Vector3 normal{};
    const bool hit = SweptCapsuleVsAABB(c, motion, floor, toi, normal);
    EXPECT_TRUE(hit);
    EXPECT_GE(toi, 0.0f);
    EXPECT_LE(toi, 1.0f);
    EXPECT_GT(normal.y, 0.5f); // 床上面の法線は +Y 寄り
}

TEST(SweptCapsuleTest, HitImmediatelyWhenStartingInContact)
{
    // 既に接触しているケース: Capsule 底端 = 床上面でわずかにめり込ませる
    Capsule c = MakeCapsule({0.0f, 0.9f, 0.0f}); // 底端 = 0.0、床上面 = 0.5、めり込み 0.5
    AABB floor = MakeAABB({0.0f, 0.0f, 0.0f}, {5.0f, 0.5f, 5.0f});
    Vector3 motion{0.0f, -1.0f, 0.0f};

    float toi = -1.0f;
    Vector3 normal{};
    const bool hit = SweptCapsuleVsAABB(c, motion, floor, toi, normal);
    EXPECT_TRUE(hit);
    EXPECT_LT(toi, 0.5f); // 既に貫通中なら toi は小さい
}

TEST(SweptCapsuleTest, HitMovingHorizontallyIntoWall)
{
    Capsule c = MakeCapsule({-3.0f, 1.0f, 0.0f});
    AABB wall = MakeAABB({0.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 5.0f});
    Vector3 motion{4.0f, 0.0f, 0.0f}; // +X 方向に進む

    float toi = -1.0f;
    Vector3 normal{};
    const bool hit = SweptCapsuleVsAABB(c, motion, wall, toi, normal);
    EXPECT_TRUE(hit);
    EXPECT_LT(normal.x, -0.5f); // 壁西面の法線は -X 寄り
}
