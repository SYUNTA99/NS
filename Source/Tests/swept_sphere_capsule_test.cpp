#include <gtest/gtest.h>
#include <Runtime/Physics/SweptCapsule.h>

namespace
{
    using NS::Core::Vector3;
    using NS::Physics::Capsule;
    using NS::Core::Sphere;

    // 軸が縦 (axis Y) の標準プレイヤー capsule を返す
    Capsule MakeVerticalCapsule(const Vector3& center, float halfHeight, float radius)
    {
        Capsule c;
        c.center = center;
        c.axis = Vector3{0.0f, 1.0f, 0.0f};
        c.halfHeight = halfHeight;
        c.radius = radius;
        return c;
    }
} // namespace

TEST(SweptCapsuleVsSphereTest, HeadOnApproachHitsAtExpectedToi)
{
    const Capsule capsule = MakeVerticalCapsule(Vector3{0.0f, 1.0f, 0.0f}, 0.5f, 0.4f);
    const Sphere sphere{Vector3{0.0f, 1.0f, 5.0f}, 0.6f};
    const Vector3 motion{0.0f, 0.0f, 10.0f};

    float toi = 1.0f;
    Vector3 normal{};
    ASSERT_TRUE(NS::Physics::SweptCapsuleVsSphere(capsule, motion, sphere, toi, normal));
    EXPECT_NEAR(toi, 0.4f, 1e-3f);
    EXPECT_NEAR(normal.x, 0.0f, 1e-3f);
    EXPECT_NEAR(normal.y, 0.0f, 1e-3f);
    EXPECT_NEAR(normal.z, -1.0f, 1e-3f);
}

// 球が capsule の胴 (中央高さ) の真横にある場合。 両端点近似では取りこぼすが解析解は当てる
TEST(SweptCapsuleVsSphereTest, SphereBesideWaistIsHit)
{
    const Capsule capsule = MakeVerticalCapsule(Vector3{0.0f, 1.0f, 0.0f}, 1.0f, 0.4f);
    const Sphere sphere{Vector3{3.0f, 1.0f, 0.0f}, 0.6f};
    const Vector3 motion{10.0f, 0.0f, 0.0f};

    float toi = 1.0f;
    Vector3 normal{};
    ASSERT_TRUE(NS::Physics::SweptCapsuleVsSphere(capsule, motion, sphere, toi, normal));
    EXPECT_NEAR(toi, 0.2f, 1e-3f);
    EXPECT_NEAR(normal.x, -1.0f, 1e-3f);
    EXPECT_NEAR(normal.y, 0.0f, 1e-3f);
    EXPECT_NEAR(normal.z, 0.0f, 1e-3f);
}

TEST(SweptCapsuleVsSphereTest, MissReturnsFalse)
{
    const Capsule capsule = MakeVerticalCapsule(Vector3{0.0f, 1.0f, 0.0f}, 0.5f, 0.4f);
    const Sphere sphere{Vector3{0.0f, 1.0f, 5.0f}, 0.6f};
    const Vector3 motion{10.0f, 0.0f, 0.0f}; // 横へ抜けるので球には届かない

    float toi = 0.0f;
    Vector3 normal{};
    EXPECT_FALSE(NS::Physics::SweptCapsuleVsSphere(capsule, motion, sphere, toi, normal));
    EXPECT_FLOAT_EQ(toi, 1.0f);
}

TEST(SweptCapsuleVsSphereTest, AlreadyOverlappingReturnsZeroToi)
{
    const Capsule capsule = MakeVerticalCapsule(Vector3{0.0f, 1.0f, 0.0f}, 0.5f, 0.4f);
    const Sphere sphere{Vector3{0.5f, 1.0f, 0.0f}, 0.4f}; // 軸までの距離 0.5 < R 0.8
    const Vector3 motion{0.0f, 0.0f, 1.0f};

    float toi = 1.0f;
    Vector3 normal{};
    ASSERT_TRUE(NS::Physics::SweptCapsuleVsSphere(capsule, motion, sphere, toi, normal));
    EXPECT_NEAR(toi, 0.0f, 1e-4f);
}

TEST(SweptCapsuleVsCapsuleTest, ParallelVerticalHitsAtExpectedToi)
{
    const Capsule self = MakeVerticalCapsule(Vector3{0.0f, 1.0f, 0.0f}, 1.0f, 0.4f);
    const Capsule other = MakeVerticalCapsule(Vector3{3.0f, 1.0f, 0.0f}, 1.0f, 0.6f);
    const Vector3 motion{10.0f, 0.0f, 0.0f};

    float toi = 1.0f;
    Vector3 normal{};
    ASSERT_TRUE(NS::Physics::SweptCapsuleVsCapsule(self, motion, other, toi, normal));
    EXPECT_NEAR(toi, 0.2f, 1e-3f);
    EXPECT_NEAR(normal.x, -1.0f, 1e-3f);
    EXPECT_NEAR(normal.y, 0.0f, 1e-3f);
    EXPECT_NEAR(normal.z, 0.0f, 1e-3f);
}

TEST(SweptCapsuleVsCapsuleTest, MissReturnsFalse)
{
    const Capsule self = MakeVerticalCapsule(Vector3{0.0f, 1.0f, 0.0f}, 1.0f, 0.4f);
    const Capsule other = MakeVerticalCapsule(Vector3{3.0f, 1.0f, 0.0f}, 1.0f, 0.6f);
    const Vector3 motion{0.0f, 0.0f, 10.0f}; // 相手と平行に離れて進む

    float toi = 0.0f;
    Vector3 normal{};
    EXPECT_FALSE(NS::Physics::SweptCapsuleVsCapsule(self, motion, other, toi, normal));
    EXPECT_FLOAT_EQ(toi, 1.0f);
}
