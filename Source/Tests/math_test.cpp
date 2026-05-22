#include <gtest/gtest.h>

#include <Framework/Core/Math.h>

#include <type_traits>

namespace
{
    constexpr float kEpsilon = 1e-5f;
}

TEST(NsCoreMath, Deg2RadConvertsKnownAngles)
{
    EXPECT_NEAR(NS::Core::Deg2Rad(0.0f), 0.0f, kEpsilon);
    EXPECT_NEAR(NS::Core::Deg2Rad(180.0f), NS::Core::kPi, kEpsilon);
    EXPECT_NEAR(NS::Core::Deg2Rad(90.0f), NS::Core::kPi * 0.5f, kEpsilon);
}

TEST(NsCoreMath, Rad2DegInvertsDeg2Rad)
{
    EXPECT_NEAR(NS::Core::Rad2Deg(NS::Core::kPi), 180.0f, 1e-3f);
    EXPECT_NEAR(NS::Core::Rad2Deg(NS::Core::Deg2Rad(45.0f)), 45.0f, 1e-3f);
}

TEST(NsCoreMath, ClampLimitsIntAndFloat)
{
    EXPECT_EQ(NS::Core::Clamp(5, 0, 10), 5);
    EXPECT_EQ(NS::Core::Clamp(-3, 0, 10), 0);
    EXPECT_EQ(NS::Core::Clamp(15, 0, 10), 10);
    EXPECT_FLOAT_EQ(NS::Core::Clamp(0.5f, 0.0f, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(NS::Core::Clamp(-1.0f, 0.0f, 1.0f), 0.0f);
}

TEST(NsCoreMath, LerpInterpolatesEndpointsAndMidpoint)
{
    EXPECT_FLOAT_EQ(NS::Core::Lerp(0.0f, 10.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(NS::Core::Lerp(0.0f, 10.0f, 1.0f), 10.0f);
    EXPECT_FLOAT_EQ(NS::Core::Lerp(0.0f, 10.0f, 0.5f), 5.0f);
}

TEST(NsCoreMath, Vector3AddDotCross)
{
    NS::Core::Vector3 a(1.0f, 2.0f, 3.0f);
    NS::Core::Vector3 b(4.0f, 5.0f, 6.0f);

    NS::Core::Vector3 sum = a + b;
    EXPECT_FLOAT_EQ(sum.x, 5.0f);
    EXPECT_FLOAT_EQ(sum.y, 7.0f);
    EXPECT_FLOAT_EQ(sum.z, 9.0f);

    EXPECT_FLOAT_EQ(a.Dot(b), 1.0f * 4.0f + 2.0f * 5.0f + 3.0f * 6.0f);

    NS::Core::Vector3 cross = a.Cross(b);
    EXPECT_FLOAT_EQ(cross.x, -3.0f);
    EXPECT_FLOAT_EQ(cross.y, 6.0f);
    EXPECT_FLOAT_EQ(cross.z, -3.0f);
}

TEST(NsCoreMath, Vector3NormalizeProducesUnitLength)
{
    NS::Core::Vector3 v(3.0f, 0.0f, 4.0f);
    v.Normalize();
    EXPECT_NEAR(v.Length(), 1.0f, kEpsilon);
}

TEST(NsCoreMath, MatrixIdentityActsAsMultiplicativeUnit)
{
    NS::Core::Matrix t = NS::Core::Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    NS::Core::Matrix r = NS::Core::Matrix::Identity * t;

    EXPECT_FLOAT_EQ(r._41, 1.0f);
    EXPECT_FLOAT_EQ(r._42, 2.0f);
    EXPECT_FLOAT_EQ(r._43, 3.0f);
}

TEST(NsCoreMath, Matrix4x4IsAliasOfMatrix)
{
    static_assert(std::is_same_v<NS::Core::Matrix, NS::Core::Matrix4x4>,
                  "Matrix4x4 は Matrix の別名でなければならない");
}

TEST(NsCoreMath, QuaternionAxisAngleRotatesVector)
{
    // Z軸 180度回転で (1,0,0) -> (-1,0,0)
    NS::Core::Quaternion q =
        NS::Core::Quaternion::CreateFromAxisAngle(NS::Core::Vector3(0.0f, 0.0f, 1.0f), NS::Core::Deg2Rad(180.0f));

    NS::Core::Vector3 rotated = NS::Core::Vector3::Transform(NS::Core::Vector3(1.0f, 0.0f, 0.0f), q);

    EXPECT_NEAR(rotated.x, -1.0f, 1e-4f);
    EXPECT_NEAR(rotated.y, 0.0f, 1e-4f);
    EXPECT_NEAR(rotated.z, 0.0f, 1e-4f);
}

TEST(NsCoreMath, AabbIntersectsOverlappingAndDisjoint)
{
    using DirectX::XMFLOAT3;
    NS::Core::AABB a(XMFLOAT3{0.0f, 0.0f, 0.0f}, XMFLOAT3{1.0f, 1.0f, 1.0f});

    NS::Core::AABB b(XMFLOAT3{0.5f, 0.0f, 0.0f}, XMFLOAT3{1.0f, 1.0f, 1.0f});
    EXPECT_TRUE(a.Intersects(b));

    NS::Core::AABB c(XMFLOAT3{5.0f, 0.0f, 0.0f}, XMFLOAT3{1.0f, 1.0f, 1.0f});
    EXPECT_FALSE(a.Intersects(c));
}
