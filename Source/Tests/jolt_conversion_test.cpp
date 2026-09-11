#include <Runtime/Core/Math.h>
#include <Runtime/Physics/detail/JoltConversion.h>

#include <gtest/gtest.h>

namespace
{
    using NS::Core::Quaternion;
    using NS::Core::Vector3;
    using NS::Physics::FromJolt;
    using NS::Physics::ToJolt;

    constexpr float k_Epsilon = 1.0e-6f;
} // namespace

TEST(JoltConversion, Vector3KeepsEachComponent)
{
    const JPH::Vec3 converted = ToJolt(Vector3{1.0f, 2.0f, 3.0f});

    EXPECT_FLOAT_EQ(converted.GetX(), 1.0f);
    EXPECT_FLOAT_EQ(converted.GetY(), 2.0f);
    EXPECT_FLOAT_EQ(converted.GetZ(), 3.0f);
}

TEST(JoltConversion, Vector3RoundTrips)
{
    const Vector3 source{-4.5f, 0.25f, 1234.5f};

    const Vector3 result = FromJolt(ToJolt(source));

    EXPECT_FLOAT_EQ(result.x, source.x);
    EXPECT_FLOAT_EQ(result.y, source.y);
    EXPECT_FLOAT_EQ(result.z, source.z);
}

TEST(JoltConversion, QuaternionKeepsEachComponent)
{
    const JPH::Quat converted = ToJolt(Quaternion{0.1f, 0.2f, 0.3f, 0.9f});

    EXPECT_FLOAT_EQ(converted.GetX(), 0.1f);
    EXPECT_FLOAT_EQ(converted.GetY(), 0.2f);
    EXPECT_FLOAT_EQ(converted.GetZ(), 0.3f);
    EXPECT_FLOAT_EQ(converted.GetW(), 0.9f);
}

TEST(JoltConversion, QuaternionRoundTrips)
{
    const Quaternion source = Quaternion::CreateFromYawPitchRoll(0.4f, -0.2f, 1.1f);

    const Quaternion result = FromJolt(ToJolt(source));

    EXPECT_NEAR(result.x, source.x, k_Epsilon);
    EXPECT_NEAR(result.y, source.y, k_Epsilon);
    EXPECT_NEAR(result.z, source.z, k_Epsilon);
    EXPECT_NEAR(result.w, source.w, k_Epsilon);
}

TEST(JoltConversion, RotatedVectorMatchesBothSides)
{
    // 成分に 0 や同じ値があると成分の取り違えが打ち消される。全成分を別の値にし、回転も 3 軸かける
    const Vector3 source{1.0f, 2.0f, 3.0f};
    const Quaternion rotation = Quaternion::CreateFromYawPitchRoll(0.7f, -0.4f, 1.2f);

    const Vector3 byNs = Vector3::Transform(source, rotation);
    const Vector3 byJolt = FromJolt(ToJolt(rotation) * ToJolt(source));

    EXPECT_NEAR(byJolt.x, byNs.x, k_Epsilon);
    EXPECT_NEAR(byJolt.y, byNs.y, k_Epsilon);
    EXPECT_NEAR(byJolt.z, byNs.z, k_Epsilon);
}
