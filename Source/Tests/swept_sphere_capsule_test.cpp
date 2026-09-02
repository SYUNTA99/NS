#include <Runtime/Physics/SweptCapsule.h>
#include <gtest/gtest.h>

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

// 相手が短く、自分の胴体の真ん中だけに当たる形。軸方向の区間は自分 0.0〜2.0 と相手 0.95〜1.05 で重なるので、
// 接触は水平距離だけで決まる。半径の和 0.8 まで詰めた x = 2.2 を 10 で割って toi 0.22
// 自分の軸の端点 2 つから飛ばす光線ではこの形が丸ごと抜ける
TEST(SweptCapsuleVsCapsuleTest, ShortOtherAgainstWaistIsHit)
{
    const Capsule self = MakeVerticalCapsule(Vector3{0.0f, 1.0f, 0.0f}, 1.0f, 0.4f);
    const Capsule other = MakeVerticalCapsule(Vector3{3.0f, 1.0f, 0.0f}, 0.05f, 0.4f);
    const Vector3 motion{10.0f, 0.0f, 0.0f};

    float toi = 1.0f;
    Vector3 normal{};
    ASSERT_TRUE(NS::Physics::SweptCapsuleVsCapsule(self, motion, other, toi, normal));
    EXPECT_NEAR(toi, 0.22f, 1e-3f);
    EXPECT_NEAR(normal.x, -1.0f, 1e-3f);
    EXPECT_NEAR(normal.y, 0.0f, 1e-3f);
    EXPECT_NEAR(normal.z, 0.0f, 1e-3f);
}

// 軸方向へずれた平行 capsule。自分の上端 1.5 と相手の下端 2.1 の間が 0.6 空くので、端の半球どうしが触れるのは
// 水平距離^2 + 0.6^2 = 0.8^2 を満たす 0.52915 の時。x = 2.47085 を 10 で割って toi 0.247085
// 軸方向の区間の重なりだけで判定すると 0.22 か接触なしになる
TEST(SweptCapsuleVsCapsuleTest, ParallelAxialOffsetUsesRoundedEnd)
{
    const Capsule self = MakeVerticalCapsule(Vector3{0.0f, 1.0f, 0.0f}, 0.5f, 0.4f);
    const Capsule other = MakeVerticalCapsule(Vector3{3.0f, 2.6f, 0.0f}, 0.5f, 0.4f);
    const Vector3 motion{10.0f, 0.0f, 0.0f};

    float toi = 1.0f;
    Vector3 normal{};
    ASSERT_TRUE(NS::Physics::SweptCapsuleVsCapsule(self, motion, other, toi, normal));
    EXPECT_NEAR(toi, 0.24708f, 1e-3f);
}

// 軸が直交する相手は端点近似のまま。横倒しの相手の左端 (2,1,0) の半球へ自分の端点が触れる形で toi 0.1376 が返る
// 平行の厳密解を足しても非平行の経路が動いていないことの見張り
TEST(SweptCapsuleVsCapsuleTest, NonParallelStillFallsBackToApproximation)
{
    const Capsule self = MakeVerticalCapsule(Vector3{0.0f, 1.0f, 0.0f}, 0.5f, 0.4f);
    Capsule other = MakeVerticalCapsule(Vector3{3.0f, 1.0f, 0.0f}, 1.0f, 0.4f);
    other.axis = Vector3{1.0f, 0.0f, 0.0f};
    const Vector3 motion{10.0f, 0.0f, 0.0f};

    float toi = 1.0f;
    Vector3 normal{};
    ASSERT_TRUE(NS::Physics::SweptCapsuleVsCapsule(self, motion, other, toi, normal));
    EXPECT_GE(toi, 0.0f);
    EXPECT_LE(toi, 1.0f);
    EXPECT_LT(normal.x, -0.5f);
}
