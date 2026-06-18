#include <gtest/gtest.h>

#include <Framework/Math/Math.h>
#include <Framework/Physics/Capsule.h>
#include <Framework/Physics/SweptOBB.h>

#include <cmath>

namespace
{
    using NS::Math::Quaternion;
    using NS::Math::Vector3;
    using NS::Physics::Capsule;
    using NS::Physics::MakeObb;
    using NS::Physics::OBB;
    using NS::Physics::SweptCapsuleVsOBB;

    constexpr float kPi = 3.14159265358979323846f;

    Capsule MakeCapsule(const Vector3& center, float radius = 0.4f, float halfHeight = 0.5f)
    {
        Capsule c;
        c.center = center;
        c.axis = {0.0f, 1.0f, 0.0f};
        c.radius = radius;
        c.halfHeight = halfHeight;
        return c;
    }

    float Dot(const Vector3& a, const Vector3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
} // namespace

// 単位回転の OBB は AABB と同じ挙動 (X+ へ動いて手前の面に当たる)
TEST(SweptObbTest, IdentityObbBehavesLikeAabb)
{
    OBB obb = MakeObb({0.0f, 0.0f, 0.0f}, Quaternion::Identity, {0.5f, 0.5f, 0.5f});
    Capsule c = MakeCapsule({-3.0f, 0.0f, 0.0f});
    Vector3 motion{4.0f, 0.0f, 0.0f};

    float toi = -1.0f;
    Vector3 normal{};
    const bool hit = SweptCapsuleVsOBB(c, motion, obb, toi, normal);

    EXPECT_TRUE(hit);
    EXPECT_NEAR(toi, (2.1f / 4.0f), 1e-3f);
    EXPECT_GT(-normal.x, 0.99f);
}

// 反対方向に動く時は当たらない
TEST(SweptObbTest, NoHitWhenMotionPointsAway)
{
    OBB obb = MakeObb({0.0f, 0.0f, 0.0f}, Quaternion::Identity, {0.5f, 0.5f, 0.5f});
    Capsule c = MakeCapsule({-3.0f, 0.0f, 0.0f});
    Vector3 motion{-4.0f, 0.0f, 0.0f};

    float toi = -1.0f;
    Vector3 normal{};
    const bool hit = SweptCapsuleVsOBB(c, motion, obb, toi, normal);

    EXPECT_FALSE(hit);
    EXPECT_FLOAT_EQ(toi, 1.0f);
}

// 45 度 Y 回転した箱: 法線は world X と直交側を向く (回転が当たりに反映される)
TEST(SweptObbTest, RotatedObbProducesRotatedNormal)
{
    const Quaternion rot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, kPi / 4.0f);
    OBB obb = MakeObb({0.0f, 0.0f, 0.0f}, rot, {0.5f, 0.5f, 0.5f});
    Capsule c = MakeCapsule({-3.0f, 0.0f, 0.0f});
    Vector3 motion{4.0f, 0.0f, 0.0f};

    float toi = -1.0f;
    Vector3 normal{};
    const bool hit = SweptCapsuleVsOBB(c, motion, obb, toi, normal);

    EXPECT_TRUE(hit);
    EXPECT_LT(std::abs(normal.x), 0.99f);
    EXPECT_NEAR(Dot(normal, normal), 1.0f, 1e-3f);
}

// 非一様 scale: X 方向に 4 倍 (halfExtents.x = 2) の箱は遠くで当たる
TEST(SweptObbTest, NonUniformScaleExtendsHitFace)
{
    OBB obb = MakeObb({0.0f, 0.0f, 0.0f}, Quaternion::Identity, {2.0f, 0.5f, 0.5f});
    Capsule c = MakeCapsule({-5.0f, 0.0f, 0.0f});
    Vector3 motion{6.0f, 0.0f, 0.0f};

    float toi = -1.0f;
    Vector3 normal{};
    const bool hit = SweptCapsuleVsOBB(c, motion, obb, toi, normal);

    EXPECT_TRUE(hit);
    EXPECT_NEAR(toi, (2.6f / 6.0f), 1e-3f);
}

// MakeObb は quaternion から正規直交な軸を作る
TEST(SweptObbTest, MakeObbBuildsOrthonormalAxes)
{
    const Quaternion rot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, kPi / 2.0f);
    OBB obb = MakeObb({1.0f, 2.0f, 3.0f}, rot, {0.5f, 0.5f, 0.5f});

    EXPECT_NEAR(Dot(obb.axisX, obb.axisX), 1.0f, 1e-4f);
    EXPECT_NEAR(Dot(obb.axisY, obb.axisY), 1.0f, 1e-4f);
    EXPECT_NEAR(Dot(obb.axisX, obb.axisY), 0.0f, 1e-4f);
    EXPECT_NEAR(Dot(obb.axisX, obb.axisZ), 0.0f, 1e-4f);
    EXPECT_NEAR(Dot(obb.axisX, Vector3::UnitX), 0.0f, 1e-4f);
}
