#include <gtest/gtest.h>

#include <Framework/Core/Math.h>
#include <Framework/Physics/Capsule.h>
#include <Framework/Physics/SweptTriangle.h>

#include <cmath>

namespace
{
    using NS::Core::Vector3;
    using NS::Physics::Capsule;
    using NS::Physics::SweptCapsuleVsTriangle;
    using NS::Physics::Triangle;

    constexpr float kPi = 3.14159265358979323846f;

    Capsule MakeCapsule(const Vector3& center, float radius = 0.4f, float halfHeight = 0.5f) noexcept
    {
        Capsule c;
        c.center = center;
        c.axis = {0.0f, 1.0f, 0.0f};
        c.radius = radius;
        c.halfHeight = halfHeight;
        return c;
    }

    /// 角度 `angleDeg` で +Z 方向に上昇する 1×1 wedge slope の slope quad を 1 三角形として返す。
    /// CCW winding で計算した normal は (0, cos(angle), -sin(angle)) になる。
    Triangle MakeWedgeSlopeTriangle(float angleDeg) noexcept
    {
        const float t = std::tan(angleDeg * kPi / 180.0f);
        const float height = t * 1.0f;
        Triangle tri;
        tri.v0 = {-1.0f, 0.0f, -0.5f};
        tri.v1 = {1.0f, height, 0.5f};
        tri.v2 = {1.0f, 0.0f, -0.5f};
        return tri;
    }
} // namespace

TEST(SweptTriangleTest, FortyFiveDegreeAscent)
{
    const Triangle tri = MakeWedgeSlopeTriangle(45.0f);
    const Capsule c = MakeCapsule({0.0f, 1.0f, -1.0f});
    const Vector3 motion{0.0f, 0.0f, 2.0f};

    float toi = -1.0f;
    Vector3 normal{};
    const bool hit = SweptCapsuleVsTriangle(c, motion, tri, toi, normal);

    EXPECT_TRUE(hit);
    EXPECT_GE(toi, 0.0f);
    EXPECT_LT(toi, 1.0f);
    const float expectedY = std::cos(45.0f * kPi / 180.0f);
    EXPECT_NEAR(normal.y, expectedY, 5e-3f);
}

TEST(SweptTriangleTest, ThirtyDegreeAscent)
{
    const Triangle tri = MakeWedgeSlopeTriangle(30.0f);
    const Capsule c = MakeCapsule({0.0f, 1.0f, -1.0f});
    const Vector3 motion{0.0f, 0.0f, 2.0f};

    float toi = -1.0f;
    Vector3 normal{};
    const bool hit = SweptCapsuleVsTriangle(c, motion, tri, toi, normal);

    EXPECT_TRUE(hit);
    EXPECT_GE(toi, 0.0f);
    EXPECT_LT(toi, 1.0f);
    const float expectedY = std::cos(30.0f * kPi / 180.0f);
    EXPECT_NEAR(normal.y, expectedY, 5e-3f);
}

TEST(SweptTriangleTest, TwentyTwoDegreeAscent)
{
    const Triangle tri = MakeWedgeSlopeTriangle(22.5f);
    const Capsule c = MakeCapsule({0.0f, 1.0f, -1.0f});
    const Vector3 motion{0.0f, 0.0f, 2.0f};

    float toi = -1.0f;
    Vector3 normal{};
    const bool hit = SweptCapsuleVsTriangle(c, motion, tri, toi, normal);

    EXPECT_TRUE(hit);
    EXPECT_GE(toi, 0.0f);
    EXPECT_LT(toi, 1.0f);
    const float expectedY = std::cos(22.5f * kPi / 180.0f);
    EXPECT_NEAR(normal.y, expectedY, 5e-3f);
}

TEST(SweptTriangleTest, FifteenDegreeAscent)
{
    const Triangle tri = MakeWedgeSlopeTriangle(15.0f);
    const Capsule c = MakeCapsule({0.0f, 1.0f, -1.0f});
    const Vector3 motion{0.0f, 0.0f, 2.0f};

    float toi = -1.0f;
    Vector3 normal{};
    const bool hit = SweptCapsuleVsTriangle(c, motion, tri, toi, normal);

    EXPECT_TRUE(hit);
    EXPECT_GE(toi, 0.0f);
    EXPECT_LT(toi, 1.0f);
    const float expectedY = std::cos(15.0f * kPi / 180.0f);
    EXPECT_NEAR(normal.y, expectedY, 5e-3f);
}

TEST(SweptTriangleTest, NoIntersectReturnsToi1)
{
    const Triangle tri = MakeWedgeSlopeTriangle(45.0f);
    // motion は slope 表面に沿う方向 (+X 軸方向) — face normal とは直交、 三角形に交わらない。
    const Capsule c = MakeCapsule({-5.0f, 5.0f, 0.0f});
    const Vector3 motion{2.0f, 0.0f, 0.0f};

    float toi = -1.0f;
    Vector3 normal{};
    const bool hit = SweptCapsuleVsTriangle(c, motion, tri, toi, normal);

    EXPECT_FALSE(hit);
    EXPECT_FLOAT_EQ(toi, 1.0f);
}

TEST(SweptTriangleTest, FloorVsWallClassification)
{
    // contactNormal.y > 0.7 を floor、 それ以下を wall とする境界判定。
    // CharacterController 側の判定なので、 ここでは normal.y の値そのものを assert する。
    {
        const Triangle tri = MakeWedgeSlopeTriangle(45.0f);
        const Capsule c = MakeCapsule({0.0f, 1.0f, -1.0f});
        const Vector3 motion{0.0f, 0.0f, 2.0f};
        float toi = 1.0f;
        Vector3 normal{};
        const bool hit = SweptCapsuleVsTriangle(c, motion, tri, toi, normal);
        EXPECT_TRUE(hit);
        // 45 度は cos(45) ≈ 0.707、 0.7 を超える → walkable floor。
        EXPECT_GT(normal.y, 0.7f);
    }
    {
        // 60 度勾配 (cos 60 = 0.5) は 0.7 以下、 wall として扱われる境界の反対側。
        const Triangle tri = MakeWedgeSlopeTriangle(60.0f);
        const Capsule c = MakeCapsule({0.0f, 1.5f, -1.0f});
        const Vector3 motion{0.0f, 0.0f, 2.0f};
        float toi = 1.0f;
        Vector3 normal{};
        const bool hit = SweptCapsuleVsTriangle(c, motion, tri, toi, normal);
        EXPECT_TRUE(hit);
        EXPECT_LT(normal.y, 0.7f);
    }
}
