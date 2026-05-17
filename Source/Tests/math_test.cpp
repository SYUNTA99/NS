#include <gtest/gtest.h>

#include <ns/core/math.h>

#include <type_traits>

namespace
{
    constexpr float kEpsilon = 1e-5f;
}

TEST(NsCoreMath, Deg2RadConvertsKnownAngles)
{
    EXPECT_NEAR(ns::core::Deg2Rad(0.0f), 0.0f, kEpsilon);
    EXPECT_NEAR(ns::core::Deg2Rad(180.0f), ns::core::kPi, kEpsilon);
    EXPECT_NEAR(ns::core::Deg2Rad(90.0f), ns::core::kPi * 0.5f, kEpsilon);
}

TEST(NsCoreMath, Rad2DegInvertsDeg2Rad)
{
    EXPECT_NEAR(ns::core::Rad2Deg(ns::core::kPi), 180.0f, 1e-3f);
    EXPECT_NEAR(ns::core::Rad2Deg(ns::core::Deg2Rad(45.0f)), 45.0f, 1e-3f);
}

TEST(NsCoreMath, ClampLimitsIntAndFloat)
{
    EXPECT_EQ(ns::core::Clamp(5, 0, 10), 5);
    EXPECT_EQ(ns::core::Clamp(-3, 0, 10), 0);
    EXPECT_EQ(ns::core::Clamp(15, 0, 10), 10);
    EXPECT_FLOAT_EQ(ns::core::Clamp(0.5f, 0.0f, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(ns::core::Clamp(-1.0f, 0.0f, 1.0f), 0.0f);
}

TEST(NsCoreMath, LerpInterpolatesEndpointsAndMidpoint)
{
    EXPECT_FLOAT_EQ(ns::core::Lerp(0.0f, 10.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ns::core::Lerp(0.0f, 10.0f, 1.0f), 10.0f);
    EXPECT_FLOAT_EQ(ns::core::Lerp(0.0f, 10.0f, 0.5f), 5.0f);
}

TEST(NsCoreMath, Vector3AddDotCross)
{
    ns::core::Vector3 a(1.0f, 2.0f, 3.0f);
    ns::core::Vector3 b(4.0f, 5.0f, 6.0f);

    ns::core::Vector3 sum = a + b;
    EXPECT_FLOAT_EQ(sum.x, 5.0f);
    EXPECT_FLOAT_EQ(sum.y, 7.0f);
    EXPECT_FLOAT_EQ(sum.z, 9.0f);

    EXPECT_FLOAT_EQ(a.Dot(b), 1.0f * 4.0f + 2.0f * 5.0f + 3.0f * 6.0f);

    ns::core::Vector3 cross = a.Cross(b);
    EXPECT_FLOAT_EQ(cross.x, -3.0f);
    EXPECT_FLOAT_EQ(cross.y, 6.0f);
    EXPECT_FLOAT_EQ(cross.z, -3.0f);
}

TEST(NsCoreMath, Vector3NormalizeProducesUnitLength)
{
    ns::core::Vector3 v(3.0f, 0.0f, 4.0f);
    v.Normalize();
    EXPECT_NEAR(v.Length(), 1.0f, kEpsilon);
}

TEST(NsCoreMath, MatrixIdentityActsAsMultiplicativeUnit)
{
    ns::core::Matrix t = ns::core::Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    ns::core::Matrix r = ns::core::Matrix::Identity * t;

    EXPECT_FLOAT_EQ(r._41, 1.0f);
    EXPECT_FLOAT_EQ(r._42, 2.0f);
    EXPECT_FLOAT_EQ(r._43, 3.0f);
}

TEST(NsCoreMath, Matrix4x4IsAliasOfMatrix)
{
    static_assert(std::is_same_v<ns::core::Matrix, ns::core::Matrix4x4>,
                  "Matrix4x4 は Matrix の別名でなければならない");
}

TEST(NsCoreMath, QuaternionAxisAngleRotatesVector)
{
    // Z軸 180度回転で (1,0,0) -> (-1,0,0)
    ns::core::Quaternion q =
        ns::core::Quaternion::CreateFromAxisAngle(ns::core::Vector3(0.0f, 0.0f, 1.0f), ns::core::Deg2Rad(180.0f));

    ns::core::Vector3 rotated = ns::core::Vector3::Transform(ns::core::Vector3(1.0f, 0.0f, 0.0f), q);

    EXPECT_NEAR(rotated.x, -1.0f, 1e-4f);
    EXPECT_NEAR(rotated.y, 0.0f, 1e-4f);
    EXPECT_NEAR(rotated.z, 0.0f, 1e-4f);
}

TEST(NsCoreMath, AabbIntersectsOverlappingAndDisjoint)
{
    using DirectX::XMFLOAT3;
    ns::core::AABB a(XMFLOAT3{0.0f, 0.0f, 0.0f}, XMFLOAT3{1.0f, 1.0f, 1.0f});

    ns::core::AABB b(XMFLOAT3{0.5f, 0.0f, 0.0f}, XMFLOAT3{1.0f, 1.0f, 1.0f});
    EXPECT_TRUE(a.Intersects(b));

    ns::core::AABB c(XMFLOAT3{5.0f, 0.0f, 0.0f}, XMFLOAT3{1.0f, 1.0f, 1.0f});
    EXPECT_FALSE(a.Intersects(c));
}
