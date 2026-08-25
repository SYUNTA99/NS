#include <Runtime/Core/Math.h>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <type_traits>

namespace
{
    constexpr float k_Epsilon = 1e-5f;
}

TEST(NsMath, DegreesToRadiansConvertsKnownAngles)
{
    EXPECT_NEAR(NS::Core::DegreesToRadians(0.0f), 0.0f, k_Epsilon);
    EXPECT_NEAR(NS::Core::DegreesToRadians(180.0f), NS::Core::k_Pi, k_Epsilon);
    EXPECT_NEAR(NS::Core::DegreesToRadians(90.0f), NS::Core::k_Pi * 0.5f, k_Epsilon);
}

TEST(NsMath, RadiansToDegreesInvertsDegreesToRadians)
{
    EXPECT_NEAR(NS::Core::RadiansToDegrees(NS::Core::k_Pi), 180.0f, 1e-3f);
    EXPECT_NEAR(NS::Core::RadiansToDegrees(NS::Core::DegreesToRadians(45.0f)), 45.0f, 1e-3f);
}

TEST(NsMath, ToRadiansFromDegreesKnownAngles)
{
    EXPECT_NEAR(NS::Core::ToRadians(NS::Core::Degrees{0.0f}).value, 0.0f, k_Epsilon);
    EXPECT_NEAR(NS::Core::ToRadians(NS::Core::Degrees{180.0f}).value, NS::Core::k_Pi, k_Epsilon);
    EXPECT_NEAR(NS::Core::ToRadians(NS::Core::Degrees{90.0f}).value, NS::Core::k_Pi * 0.5f, k_Epsilon);
}

TEST(NsMath, ToDegreesInvertsToRadians)
{
    const auto rt = NS::Core::ToDegrees(NS::Core::ToRadians(NS::Core::Degrees{60.0f}));
    EXPECT_NEAR(rt.value, 60.0f, 1e-3f);
}

TEST(NsMath, RadiansEqualityComparesValue)
{
    EXPECT_TRUE(NS::Core::Radians{1.0f} == NS::Core::Radians{1.0f});
    EXPECT_FALSE(NS::Core::Radians{1.0f} == NS::Core::Radians{1.1f});
    EXPECT_TRUE(NS::Core::Degrees{45.0f} == NS::Core::Degrees{45.0f});
}

// 暗黙変換の禁止を static_assert で確認する
static_assert(!std::is_convertible_v<float, NS::Core::Radians>, "float から Radians への暗黙変換は禁止");
static_assert(!std::is_convertible_v<NS::Core::Degrees, NS::Core::Radians>,
              "Degrees から Radians への暗黙変換は禁止 (ToRadians 経由のみ)");
static_assert(!std::is_convertible_v<NS::Core::Radians, NS::Core::Degrees>,
              "Radians から Degrees への暗黙変換は禁止 (ToDegrees 経由のみ)");

// constexpr で全関数が動くこと
static_assert(NS::Core::ToRadians(NS::Core::Degrees{0.0f}).value == 0.0f);
static_assert(NS::Core::ToDegrees(NS::Core::Radians{0.0f}).value == 0.0f);
static_assert(NS::Core::Radians{1.0f} == NS::Core::Radians{1.0f});

TEST(NsMath, Size2DEqualityComparesBothDimensions)
{
    // 波括弧内のコンマで EXPECT_TRUE マクロが分裂しないよう括弧でくくって1引数にする
    EXPECT_TRUE((NS::Core::Size2D{1280, 720} == NS::Core::Size2D{1280, 720}));
    EXPECT_TRUE((NS::Core::Size2D{1280, 720} != NS::Core::Size2D{1920, 720}));
    EXPECT_TRUE((NS::Core::Size2D{1280, 720} != NS::Core::Size2D{1280, 1080}));
}

TEST(NsMath, Size2DAspectRatioMatches16Over9)
{
    const NS::Core::Size2D s{1920, 1080};
    EXPECT_NEAR(NS::Core::AspectRatio(s), 16.0f / 9.0f, 1e-5f);
}

TEST(NsMath, Size2DSwappedDimensionsCompareUnequal)
{
    EXPECT_TRUE((NS::Core::Size2D{640, 480} != NS::Core::Size2D{480, 640}));
}

// width/height の取り違え事故を型システムで防止する
static_assert(!std::is_convertible_v<int, NS::Core::Size2D>, "int から Size2D への暗黙変換は禁止");

static_assert(NS::Core::Size2D{4, 2} == NS::Core::Size2D{4, 2});
static_assert(NS::Core::AspectRatio(NS::Core::Size2D{2, 1}) == 2.0f);

TEST(NsMath, ClampLimitsIntAndFloat)
{
    EXPECT_EQ(NS::Core::Clamp(5, 0, 10), 5);
    EXPECT_EQ(NS::Core::Clamp(-3, 0, 10), 0);
    EXPECT_EQ(NS::Core::Clamp(15, 0, 10), 10);
    EXPECT_FLOAT_EQ(NS::Core::Clamp(0.5f, 0.0f, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(NS::Core::Clamp(-1.0f, 0.0f, 1.0f), 0.0f);
}

TEST(NsMath, LerpInterpolatesEndpointsAndMidpoint)
{
    EXPECT_FLOAT_EQ(NS::Core::Lerp(0.0f, 10.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(NS::Core::Lerp(0.0f, 10.0f, 1.0f), 10.0f);
    EXPECT_FLOAT_EQ(NS::Core::Lerp(0.0f, 10.0f, 0.5f), 5.0f);
}

TEST(NsMath, Vector3AddDotCross)
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

TEST(NsMath, Vector3NormalizeProducesUnitLength)
{
    NS::Core::Vector3 v(3.0f, 0.0f, 4.0f);
    v.Normalize();
    EXPECT_NEAR(v.Length(), 1.0f, k_Epsilon);
}

TEST(NsMath, MatrixIdentityActsAsMultiplicativeUnit)
{
    NS::Core::Matrix t = NS::Core::Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    NS::Core::Matrix r = NS::Core::Matrix::Identity * t;

    EXPECT_FLOAT_EQ(r._41, 1.0f);
    EXPECT_FLOAT_EQ(r._42, 2.0f);
    EXPECT_FLOAT_EQ(r._43, 3.0f);
}

TEST(NsMath, Matrix4x4IsAliasOfMatrix)
{
    static_assert(std::is_same_v<NS::Core::Matrix, NS::Core::Matrix4x4>,
                  "Matrix4x4 は Matrix の別名でなければならない");
}

TEST(NsMath, QuaternionAxisAngleRotatesVector)
{
    // Z軸 180度回転で (1,0,0) -> (-1,0,0)
    NS::Core::Quaternion q = NS::Core::Quaternion::CreateFromAxisAngle(NS::Core::Vector3(0.0f, 0.0f, 1.0f),
                                                                       NS::Core::DegreesToRadians(180.0f));

    NS::Core::Vector3 rotated = NS::Core::Vector3::Transform(NS::Core::Vector3(1.0f, 0.0f, 0.0f), q);

    EXPECT_NEAR(rotated.x, -1.0f, 1e-4f);
    EXPECT_NEAR(rotated.y, 0.0f, 1e-4f);
    EXPECT_NEAR(rotated.z, 0.0f, 1e-4f);
}

TEST(NsMath, EpsilonHoldsItsExactValue)
{
    EXPECT_EQ(NS::Core::k_Epsilon, 1e-4f);
}

TEST(NsMath, FloatEpsilonMatchesNumericLimits)
{
    EXPECT_EQ(NS::Core::k_FloatEpsilon, std::numeric_limits<float>::epsilon());
}

TEST(NsMath, TryNormalizeHorizontalRejectsZeroVectorAndKeepsOutput)
{
    NS::Core::Vector3 out{7.0f, 8.0f, 9.0f};
    EXPECT_FALSE(NS::Core::TryNormalizeHorizontal(NS::Core::Vector3{0.0f, 0.0f, 0.0f}, out));
    EXPECT_EQ(out.x, 7.0f);
    EXPECT_EQ(out.y, 8.0f);
    EXPECT_EQ(out.z, 9.0f);
}

TEST(NsMath, TryNormalizeHorizontalRejectsBelowEpsilon)
{
    NS::Core::Vector3 out{1.0f, 1.0f, 1.0f};
    EXPECT_FALSE(NS::Core::TryNormalizeHorizontal(NS::Core::Vector3{5e-5f, 100.0f, 0.0f}, out));
    EXPECT_EQ(out.x, 1.0f);
    EXPECT_EQ(out.y, 1.0f);
    EXPECT_EQ(out.z, 1.0f);
}

TEST(NsMath, TryNormalizeHorizontalDropsVerticalAndNormalizesXZ)
{
    NS::Core::Vector3 out{0.0f, 0.0f, 0.0f};
    EXPECT_TRUE(NS::Core::TryNormalizeHorizontal(NS::Core::Vector3{3.0f, 99.0f, 4.0f}, out));
    EXPECT_FLOAT_EQ(out.x, 0.6f);
    EXPECT_FLOAT_EQ(out.y, 0.0f);
    EXPECT_FLOAT_EQ(out.z, 0.8f);
}

TEST(NsMath, TryNormalizeHorizontalMatchesCallerArithmeticBitForBit)
{
    const NS::Core::Vector3 v{1.7f, -3.0f, -0.35f};
    const float lengthSq = v.x * v.x + v.z * v.z;
    const float invLength = 1.0f / std::sqrt(lengthSq);

    NS::Core::Vector3 out{0.0f, 0.0f, 0.0f};
    ASSERT_TRUE(NS::Core::TryNormalizeHorizontal(v, out));
    EXPECT_EQ(out.x, v.x * invLength);
    EXPECT_EQ(out.y, 0.0f);
    EXPECT_EQ(out.z, v.z * invLength);
}

TEST(NsMath, AabbIntersectsOverlappingAndDisjoint)
{
    using DirectX::XMFLOAT3;
    NS::Core::AABB a(XMFLOAT3{0.0f, 0.0f, 0.0f}, XMFLOAT3{1.0f, 1.0f, 1.0f});

    NS::Core::AABB b(XMFLOAT3{0.5f, 0.0f, 0.0f}, XMFLOAT3{1.0f, 1.0f, 1.0f});
    EXPECT_TRUE(a.Intersects(b));

    NS::Core::AABB c(XMFLOAT3{5.0f, 0.0f, 0.0f}, XMFLOAT3{1.0f, 1.0f, 1.0f});
    EXPECT_FALSE(a.Intersects(c));
}
