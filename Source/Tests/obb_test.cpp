#include <Runtime/Core/Math.h>
#include <Runtime/Core/OBB.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Core::MakeOBB;
    using NS::Core::OBB;
    using NS::Core::Quaternion;
    using NS::Core::Vector3;

    constexpr float k_Epsilon = 1e-5f;

    void ExpectVectorNear(const Vector3& actual, const Vector3& expected)
    {
        EXPECT_NEAR(actual.x, expected.x, k_Epsilon);
        EXPECT_NEAR(actual.y, expected.y, k_Epsilon);
        EXPECT_NEAR(actual.z, expected.z, k_Epsilon);
    }
} // namespace

TEST(ObbTest, MakeObbKeepsCenterAndUnitAxesWithoutRotation)
{
    const OBB obb = MakeOBB(Vector3{1.0f, 2.0f, 3.0f}, Quaternion::Identity, Vector3{0.5f, 1.0f, 2.0f});

    ExpectVectorNear(obb.center, Vector3{1.0f, 2.0f, 3.0f});
    ExpectVectorNear(obb.axisX, Vector3::UnitX);
    ExpectVectorNear(obb.axisY, Vector3::UnitY);
    ExpectVectorNear(obb.axisZ, Vector3::UnitZ);
    EXPECT_FLOAT_EQ(obb.halfExtentX, 0.5f);
    EXPECT_FLOAT_EQ(obb.halfExtentY, 1.0f);
    EXPECT_FLOAT_EQ(obb.halfExtentZ, 2.0f);
}

TEST(ObbTest, MakeObbRotatesAxesByTheRotation)
{
    const Quaternion yaw90 = Quaternion::CreateFromAxisAngle(Vector3::UnitY, NS::Core::k_Pi * 0.5f);

    const OBB obb = MakeOBB(Vector3{0.0f, 0.0f, 0.0f}, yaw90, Vector3{1.0f, 1.0f, 1.0f});

    ExpectVectorNear(obb.axisX, Vector3{0.0f, 0.0f, -1.0f});
    ExpectVectorNear(obb.axisY, Vector3::UnitY);
    ExpectVectorNear(obb.axisZ, Vector3{1.0f, 0.0f, 0.0f});
}

TEST(ObbTest, MakeObbTakesAbsoluteHalfExtents)
{
    const OBB obb = MakeOBB(Vector3{0.0f, 0.0f, 0.0f}, Quaternion::Identity, Vector3{-0.5f, -1.0f, 2.0f});

    EXPECT_FLOAT_EQ(obb.halfExtentX, 0.5f);
    EXPECT_FLOAT_EQ(obb.halfExtentY, 1.0f);
    EXPECT_FLOAT_EQ(obb.halfExtentZ, 2.0f);
}
