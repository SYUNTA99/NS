#include <gtest/gtest.h>

#include <Framework/Math/Math.h>
#include <Framework/Physics/PhysicsWorld.h>

#include <cmath>

namespace
{
    using NS::Math::AABB;
    using NS::Math::Quaternion;
    using NS::Math::Vector3;
    using NS::Physics::Capsule;
    using NS::Physics::MakeObb;
    using NS::Physics::PhysicsWorld;
    using NS::Physics::Sphere;
    using NS::Physics::SweepHit;

    constexpr float kPi = 3.14159265358979323846f;

    AABB MakeBox(const Vector3& center, const Vector3& extents)
    {
        AABB b;
        b.Center = center;
        b.Extents = extents;
        return b;
    }

    Capsule MakeCapsule(const Vector3& center, float radius = 0.4f, float halfHeight = 0.5f)
    {
        Capsule c;
        c.center = center;
        c.axis = Vector3{0.0f, 1.0f, 0.0f};
        c.radius = radius;
        c.halfHeight = halfHeight;
        return c;
    }
} // namespace

// 既定構築の world は空
TEST(PhysicsWorldTest, DefaultIsEmpty)
{
    PhysicsWorld world;
    EXPECT_TRUE(world.IsEmpty());
    EXPECT_TRUE(world.Aabbs().empty());
}

// AABB を足すと非空になり Aabbs に反映される
TEST(PhysicsWorldTest, AddAabbReflectsInAccessors)
{
    PhysicsWorld world;
    world.AddAabb(MakeBox({1.0f, 2.0f, 3.0f}, {0.5f, 0.5f, 0.5f}));

    EXPECT_FALSE(world.IsEmpty());
    ASSERT_EQ(world.Aabbs().size(), 1u);
    EXPECT_FLOAT_EQ(world.Aabbs()[0].Center.x, 1.0f);
}

// AABB 以外の channel を足しても非空判定になる (sphere は AABB channel に入らない)
TEST(PhysicsWorldTest, NonAabbChannelsCountTowardNonEmpty)
{
    PhysicsWorld world;
    Sphere s;
    s.center = Vector3{0.0f, 0.0f, 0.0f};
    s.radius = 1.0f;
    world.AddSphere(s);

    EXPECT_FALSE(world.IsEmpty());
    EXPECT_TRUE(world.Aabbs().empty());
}

// Clear で全 channel と grid が空に戻る
TEST(PhysicsWorldTest, ClearResetsAllChannels)
{
    PhysicsWorld world;
    world.AddAabb(MakeBox({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
    world.BuildBroadphase();

    world.Clear();

    EXPECT_TRUE(world.IsEmpty());
    EXPECT_TRUE(world.Aabbs().empty());
}

// 空 world で BuildBroadphase を呼んでも安全
TEST(PhysicsWorldTest, BuildBroadphaseOnEmptyIsSafe)
{
    PhysicsWorld world;
    world.BuildBroadphase();
    EXPECT_TRUE(world.IsEmpty());
}

// AABB channel: +X 移動で手前の面に当たり、 法線は -X
TEST(PhysicsWorldTest, SweepCapsuleHitsAabb)
{
    PhysicsWorld world;
    world.AddAabb(MakeBox({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    world.BuildBroadphase();

    const SweepHit h = world.SweepCapsule(MakeCapsule({-3.0f, 0.0f, 0.0f}), Vector3{4.0f, 0.0f, 0.0f});

    EXPECT_TRUE(h.hit);
    EXPECT_NEAR(h.toi, 2.1f / 4.0f, 1e-3f);
    EXPECT_GT(-h.normal.x, 0.99f);
}

// 反対方向へ動く時は当たらない
TEST(PhysicsWorldTest, SweepCapsuleNoHitWhenAway)
{
    PhysicsWorld world;
    world.AddAabb(MakeBox({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    world.BuildBroadphase();

    const SweepHit h = world.SweepCapsule(MakeCapsule({-3.0f, 0.0f, 0.0f}), Vector3{-4.0f, 0.0f, 0.0f});

    EXPECT_FALSE(h.hit);
    EXPECT_FLOAT_EQ(h.toi, 1.0f);
}

// 複数 AABB のうち手前を最小 TOI で返す
TEST(PhysicsWorldTest, SweepCapsulePicksEarliestAabb)
{
    PhysicsWorld world;
    world.AddAabb(MakeBox({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    world.AddAabb(MakeBox({5.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    world.BuildBroadphase();

    const SweepHit h = world.SweepCapsule(MakeCapsule({-3.0f, 0.0f, 0.0f}), Vector3{10.0f, 0.0f, 0.0f});

    EXPECT_TRUE(h.hit);
    EXPECT_NEAR(h.toi, 2.1f / 10.0f, 1e-3f);
}

// grid 有無で同一結果 (broadphase は答えを変えない)
TEST(PhysicsWorldTest, SweepCapsuleGridMatchesBruteForce)
{
    PhysicsWorld brute;
    brute.AddAabb(MakeBox({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    brute.AddAabb(MakeBox({5.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    // BuildBroadphase を呼ばない -> grid 空 -> 総当たり経路

    PhysicsWorld gridded;
    gridded.AddAabb(MakeBox({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    gridded.AddAabb(MakeBox({5.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    gridded.BuildBroadphase();

    const Capsule c = MakeCapsule({-3.0f, 0.0f, 0.0f});
    const Vector3 motion{10.0f, 0.0f, 0.0f};
    const SweepHit hb = brute.SweepCapsule(c, motion);
    const SweepHit hg = gridded.SweepCapsule(c, motion);

    EXPECT_EQ(hb.hit, hg.hit);
    EXPECT_FLOAT_EQ(hb.toi, hg.toi);
    EXPECT_FLOAT_EQ(hb.normal.x, hg.normal.x);
}

// OBB channel: 45 度回転で法線が純粋な -X からずれる
TEST(PhysicsWorldTest, SweepCapsuleHitsRotatedObb)
{
    PhysicsWorld world;
    const Quaternion rot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, kPi / 4.0f);
    world.AddObb(MakeObb({0.0f, 0.0f, 0.0f}, rot, {0.5f, 0.5f, 0.5f}));

    const SweepHit h = world.SweepCapsule(MakeCapsule({-3.0f, 0.0f, 0.0f}), Vector3{4.0f, 0.0f, 0.0f});

    EXPECT_TRUE(h.hit);
    EXPECT_LT(std::abs(h.normal.x), 0.99f);
}

// Sphere channel に当たる
TEST(PhysicsWorldTest, SweepCapsuleHitsSphere)
{
    PhysicsWorld world;
    Sphere s;
    s.center = Vector3{0.0f, 0.0f, 0.0f};
    s.radius = 0.5f;
    world.AddSphere(s);

    const SweepHit h = world.SweepCapsule(MakeCapsule({-3.0f, 0.0f, 0.0f}), Vector3{4.0f, 0.0f, 0.0f});

    EXPECT_TRUE(h.hit);
    EXPECT_LT(h.toi, 1.0f);
}

// Capsule channel に当たる
TEST(PhysicsWorldTest, SweepCapsuleHitsCapsule)
{
    PhysicsWorld world;
    world.AddCapsule(MakeCapsule({0.0f, 0.0f, 0.0f}, 0.5f, 0.5f));

    const SweepHit h = world.SweepCapsule(MakeCapsule({-3.0f, 0.0f, 0.0f}), Vector3{4.0f, 0.0f, 0.0f});

    EXPECT_TRUE(h.hit);
    EXPECT_LT(h.toi, 1.0f);
}

// ProbeGround: 真下の平 AABB を reach 内で拾う
TEST(PhysicsWorldTest, ProbeGroundDetectsAabbBelow)
{
    PhysicsWorld world;
    world.AddAabb(MakeBox({0.0f, 0.0f, 0.0f}, {2.0f, 0.5f, 2.0f}));

    EXPECT_TRUE(world.ProbeGround(Vector3{0.0f, 1.0f, 0.0f}, 0.6f));
}

// ProbeGround: 真下の OBB を reach 内で拾う
TEST(PhysicsWorldTest, ProbeGroundDetectsObbBelow)
{
    PhysicsWorld world;
    world.AddObb(MakeObb({0.0f, 0.0f, 0.0f}, Quaternion::Identity, {2.0f, 0.5f, 2.0f}));

    EXPECT_TRUE(world.ProbeGround(Vector3{0.0f, 1.0f, 0.0f}, 0.6f));
}

// ProbeGround: reach 外の床は拾わない
TEST(PhysicsWorldTest, ProbeGroundFalseWhenOutOfReach)
{
    PhysicsWorld world;
    world.AddAabb(MakeBox({0.0f, 0.0f, 0.0f}, {2.0f, 0.5f, 2.0f}));

    EXPECT_FALSE(world.ProbeGround(Vector3{0.0f, 5.0f, 0.0f}, 0.6f));
}
