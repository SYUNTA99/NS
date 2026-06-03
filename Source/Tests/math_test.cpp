#include <gtest/gtest.h>

#include <Framework/Math/Math.h>

#include <type_traits>

namespace
{
    constexpr float kEpsilon = 1e-5f;
}

TEST(NsMath, DegreesToRadiansConvertsKnownAngles)
{
    EXPECT_NEAR(NS::Math::DegreesToRadians(0.0f), 0.0f, kEpsilon);
    EXPECT_NEAR(NS::Math::DegreesToRadians(180.0f), NS::Math::kPi, kEpsilon);
    EXPECT_NEAR(NS::Math::DegreesToRadians(90.0f), NS::Math::kPi * 0.5f, kEpsilon);
}

TEST(NsMath, RadiansToDegreesInvertsDegreesToRadians)
{
    EXPECT_NEAR(NS::Math::RadiansToDegrees(NS::Math::kPi), 180.0f, 1e-3f);
    EXPECT_NEAR(NS::Math::RadiansToDegrees(NS::Math::DegreesToRadians(45.0f)), 45.0f, 1e-3f);
}

TEST(NsMath, ToRadiansFromDegreesKnownAngles)
{
    EXPECT_NEAR(NS::Math::ToRadians(NS::Math::Degrees{0.0f}).value, 0.0f, kEpsilon);
    EXPECT_NEAR(NS::Math::ToRadians(NS::Math::Degrees{180.0f}).value, NS::Math::kPi, kEpsilon);
    EXPECT_NEAR(NS::Math::ToRadians(NS::Math::Degrees{90.0f}).value, NS::Math::kPi * 0.5f, kEpsilon);
}

TEST(NsMath, ToDegreesInvertsToRadians)
{
    const auto rt = NS::Math::ToDegrees(NS::Math::ToRadians(NS::Math::Degrees{60.0f}));
    EXPECT_NEAR(rt.value, 60.0f, 1e-3f);
}

TEST(NsMath, RadiansEqualityComparesValue)
{
    EXPECT_TRUE(NS::Math::Radians{1.0f} == NS::Math::Radians{1.0f});
    EXPECT_FALSE(NS::Math::Radians{1.0f} == NS::Math::Radians{1.1f});
    EXPECT_TRUE(NS::Math::Degrees{45.0f} == NS::Math::Degrees{45.0f});
}

// 暗黙変換禁止 — 以下が compile error にならない場合は型システムが壊れている
// 安全のための static_assert で型不一致を assert する
static_assert(!std::is_convertible_v<float, NS::Math::Radians>, "float から Radians への暗黙変換は禁止");
static_assert(!std::is_convertible_v<NS::Math::Degrees, NS::Math::Radians>,
              "Degrees から Radians への暗黙変換は禁止 (ToRadians 経由のみ)");
static_assert(!std::is_convertible_v<NS::Math::Radians, NS::Math::Degrees>,
              "Radians から Degrees への暗黙変換は禁止 (ToDegrees 経由のみ)");

// constexpr で全関数が動くこと
static_assert(NS::Math::ToRadians(NS::Math::Degrees{0.0f}).value == 0.0f);
static_assert(NS::Math::ToDegrees(NS::Math::Radians{0.0f}).value == 0.0f);
static_assert(NS::Math::Radians{1.0f} == NS::Math::Radians{1.0f});

TEST(NsMath, Size2DEqualityComparesBothDimensions)
{
    // brace 内のコンマで EXPECT_TRUE マクロが分裂しないよう extra paren で 1 引数にまとめる
    EXPECT_TRUE((NS::Math::Size2D{1280, 720} == NS::Math::Size2D{1280, 720}));
    EXPECT_TRUE((NS::Math::Size2D{1280, 720} != NS::Math::Size2D{1920, 720}));
    EXPECT_TRUE((NS::Math::Size2D{1280, 720} != NS::Math::Size2D{1280, 1080}));
}

TEST(NsMath, Size2DAspectRatioMatches16Over9)
{
    const NS::Math::Size2D s{1920, 1080};
    EXPECT_NEAR(NS::Math::AspectRatio(s), 16.0f / 9.0f, 1e-5f);
}

TEST(NsMath, Size2DSwappedDimensionsCompareUnequal)
{
    EXPECT_TRUE((NS::Math::Size2D{640, 480} != NS::Math::Size2D{480, 640}));
}

// width/height の取り違え事故を型システムで防止する
static_assert(!std::is_convertible_v<int, NS::Math::Size2D>, "int から Size2D への暗黙変換は禁止");

static_assert(NS::Math::Size2D{4, 2} == NS::Math::Size2D{4, 2});
static_assert(NS::Math::AspectRatio(NS::Math::Size2D{2, 1}) == 2.0f);

TEST(NsMath, ClampLimitsIntAndFloat)
{
    EXPECT_EQ(NS::Math::Clamp(5, 0, 10), 5);
    EXPECT_EQ(NS::Math::Clamp(-3, 0, 10), 0);
    EXPECT_EQ(NS::Math::Clamp(15, 0, 10), 10);
    EXPECT_FLOAT_EQ(NS::Math::Clamp(0.5f, 0.0f, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(NS::Math::Clamp(-1.0f, 0.0f, 1.0f), 0.0f);
}

TEST(NsMath, LerpInterpolatesEndpointsAndMidpoint)
{
    EXPECT_FLOAT_EQ(NS::Math::Lerp(0.0f, 10.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(NS::Math::Lerp(0.0f, 10.0f, 1.0f), 10.0f);
    EXPECT_FLOAT_EQ(NS::Math::Lerp(0.0f, 10.0f, 0.5f), 5.0f);
}

TEST(NsMath, Vector3AddDotCross)
{
    NS::Math::Vector3 a(1.0f, 2.0f, 3.0f);
    NS::Math::Vector3 b(4.0f, 5.0f, 6.0f);

    NS::Math::Vector3 sum = a + b;
    EXPECT_FLOAT_EQ(sum.x, 5.0f);
    EXPECT_FLOAT_EQ(sum.y, 7.0f);
    EXPECT_FLOAT_EQ(sum.z, 9.0f);

    EXPECT_FLOAT_EQ(a.Dot(b), 1.0f * 4.0f + 2.0f * 5.0f + 3.0f * 6.0f);

    NS::Math::Vector3 cross = a.Cross(b);
    EXPECT_FLOAT_EQ(cross.x, -3.0f);
    EXPECT_FLOAT_EQ(cross.y, 6.0f);
    EXPECT_FLOAT_EQ(cross.z, -3.0f);
}

TEST(NsMath, Vector3NormalizeProducesUnitLength)
{
    NS::Math::Vector3 v(3.0f, 0.0f, 4.0f);
    v.Normalize();
    EXPECT_NEAR(v.Length(), 1.0f, kEpsilon);
}

TEST(NsMath, MatrixIdentityActsAsMultiplicativeUnit)
{
    NS::Math::Matrix t = NS::Math::Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    NS::Math::Matrix r = NS::Math::Matrix::Identity * t;

    EXPECT_FLOAT_EQ(r._41, 1.0f);
    EXPECT_FLOAT_EQ(r._42, 2.0f);
    EXPECT_FLOAT_EQ(r._43, 3.0f);
}

TEST(NsMath, Matrix4x4IsAliasOfMatrix)
{
    static_assert(std::is_same_v<NS::Math::Matrix, NS::Math::Matrix4x4>,
                  "Matrix4x4 は Matrix の別名でなければならない");
}

TEST(NsMath, QuaternionAxisAngleRotatesVector)
{
    // Z軸 180度回転で (1,0,0) -> (-1,0,0)
    NS::Math::Quaternion q = NS::Math::Quaternion::CreateFromAxisAngle(NS::Math::Vector3(0.0f, 0.0f, 1.0f),
                                                                       NS::Math::DegreesToRadians(180.0f));

    NS::Math::Vector3 rotated = NS::Math::Vector3::Transform(NS::Math::Vector3(1.0f, 0.0f, 0.0f), q);

    EXPECT_NEAR(rotated.x, -1.0f, 1e-4f);
    EXPECT_NEAR(rotated.y, 0.0f, 1e-4f);
    EXPECT_NEAR(rotated.z, 0.0f, 1e-4f);
}

TEST(NsMath, AabbIntersectsOverlappingAndDisjoint)
{
    using DirectX::XMFLOAT3;
    NS::Math::AABB a(XMFLOAT3{0.0f, 0.0f, 0.0f}, XMFLOAT3{1.0f, 1.0f, 1.0f});

    NS::Math::AABB b(XMFLOAT3{0.5f, 0.0f, 0.0f}, XMFLOAT3{1.0f, 1.0f, 1.0f});
    EXPECT_TRUE(a.Intersects(b));

    NS::Math::AABB c(XMFLOAT3{5.0f, 0.0f, 0.0f}, XMFLOAT3{1.0f, 1.0f, 1.0f});
    EXPECT_FALSE(a.Intersects(c));
}
