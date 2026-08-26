#include <Runtime/Core/Math.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <array>
#include <cmath>
#include <gtest/gtest.h>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Quaternion;
    using NS::Core::Vector3;
    using NS::Physics::Capsule;
    using NS::Physics::MakeOBB;
    using NS::Physics::PhysicsWorld;
    using NS::Core::Sphere;
    using NS::Physics::SweepHit;

    constexpr float k_Pi = 3.14159265358979323846f;

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
    EXPECT_TRUE(world.AABBs().empty());
}

// AABB を足すと非空になり AABBs に反映される
TEST(PhysicsWorldTest, AddAabbReflectsInAccessors)
{
    PhysicsWorld world;
    world.AddAABB(MakeBox({1.0f, 2.0f, 3.0f}, {0.5f, 0.5f, 0.5f}));

    EXPECT_FALSE(world.IsEmpty());
    ASSERT_EQ(world.AABBs().size(), 1u);
    EXPECT_FLOAT_EQ(world.AABBs()[0].Center.x, 1.0f);
}

// sphere を足しても AABBs には入らないが非空判定になる
TEST(PhysicsWorldTest, NonAabbChannelsCountTowardNonEmpty)
{
    PhysicsWorld world;
    Sphere s;
    s.center = Vector3{0.0f, 0.0f, 0.0f};
    s.radius = 1.0f;
    world.AddSphere(s);

    EXPECT_FALSE(world.IsEmpty());
    EXPECT_TRUE(world.AABBs().empty());
}

// Clear で全 channel と grid が空に戻る
TEST(PhysicsWorldTest, ClearResetsAllChannels)
{
    PhysicsWorld world;
    world.AddAABB(MakeBox({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
    world.BuildBroadphase();

    world.Clear();

    EXPECT_TRUE(world.IsEmpty());
    EXPECT_TRUE(world.AABBs().empty());
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
    world.AddAABB(MakeBox({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
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
    world.AddAABB(MakeBox({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    world.BuildBroadphase();

    const SweepHit h = world.SweepCapsule(MakeCapsule({-3.0f, 0.0f, 0.0f}), Vector3{-4.0f, 0.0f, 0.0f});

    EXPECT_FALSE(h.hit);
    EXPECT_FLOAT_EQ(h.toi, 1.0f);
}

// 複数 AABB のうち手前を最小 TOI で返す
TEST(PhysicsWorldTest, SweepCapsulePicksEarliestAabb)
{
    PhysicsWorld world;
    world.AddAABB(MakeBox({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    world.AddAABB(MakeBox({5.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    world.BuildBroadphase();

    const SweepHit h = world.SweepCapsule(MakeCapsule({-3.0f, 0.0f, 0.0f}), Vector3{10.0f, 0.0f, 0.0f});

    EXPECT_TRUE(h.hit);
    EXPECT_NEAR(h.toi, 2.1f / 10.0f, 1e-3f);
}

// grid 有無で同一結果。broadphase は答えを変えない
TEST(PhysicsWorldTest, SweepCapsuleGridMatchesBruteForce)
{
    PhysicsWorld brute;
    brute.AddAABB(MakeBox({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    brute.AddAABB(MakeBox({5.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    // BuildBroadphase を呼ばない -> grid 空 -> 総当たり経路

    PhysicsWorld gridded;
    gridded.AddAABB(MakeBox({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
    gridded.AddAABB(MakeBox({5.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}));
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
    const Quaternion rot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, k_Pi / 4.0f);
    world.AddOBB(MakeOBB({0.0f, 0.0f, 0.0f}, rot, {0.5f, 0.5f, 0.5f}));

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

// 開始時に AABB 内部へ貫通している capsule は、 motion で外へ抜けるほど動かしても
// 初期貫通が優先され toi=0 + 最寄り面の押し戻し法線を返す。退化ケースを貫通させない
TEST(PhysicsWorldTest, SweepCapsuleOverlappingAabbReturnsZeroToi)
{
    PhysicsWorld world;
    world.AddAABB(MakeBox({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}));
    world.BuildBroadphase();

    const SweepHit h = world.SweepCapsule(MakeCapsule({1.5f, 0.0f, 0.0f}), Vector3{3.0f, 0.0f, 0.0f});

    EXPECT_TRUE(h.hit);
    EXPECT_FLOAT_EQ(h.toi, 0.0f);
    EXPECT_GT(h.normal.x, 0.99f); // 最寄り +X 面へ押し戻す
}

// 回転 OBB 内部から始まっても toi=0 + 単位の押し戻し法線
TEST(PhysicsWorldTest, SweepCapsuleOverlappingObbReturnsZeroToi)
{
    PhysicsWorld world;
    const Quaternion rot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, k_Pi / 4.0f);
    world.AddOBB(MakeOBB({0.0f, 0.0f, 0.0f}, rot, {2.0f, 2.0f, 2.0f}));

    const SweepHit h = world.SweepCapsule(MakeCapsule({0.0f, 0.0f, 0.0f}), Vector3{3.0f, 0.0f, 0.0f});

    EXPECT_TRUE(h.hit);
    EXPECT_FLOAT_EQ(h.toi, 0.0f);
    const float nLen = std::sqrt(h.normal.x * h.normal.x + h.normal.y * h.normal.y + h.normal.z * h.normal.z);
    EXPECT_GT(nLen, 0.99f);
}

// Sphere に深く食い込んだ状態から始まると toi=0
TEST(PhysicsWorldTest, SweepCapsuleOverlappingSphereReturnsZeroToi)
{
    PhysicsWorld world;
    Sphere s;
    s.center = Vector3{0.0f, 0.0f, 0.0f};
    s.radius = 1.0f;
    world.AddSphere(s);

    const SweepHit h = world.SweepCapsule(MakeCapsule({0.0f, 0.0f, 0.0f}), Vector3{3.0f, 0.0f, 0.0f});

    EXPECT_TRUE(h.hit);
    EXPECT_FLOAT_EQ(h.toi, 0.0f);
}

// 別 capsule と重なった状態から始まると toi=0
TEST(PhysicsWorldTest, SweepCapsuleOverlappingCapsuleReturnsZeroToi)
{
    PhysicsWorld world;
    world.AddCapsule(MakeCapsule({0.0f, 0.0f, 0.0f}, 0.5f, 0.5f));

    const SweepHit h = world.SweepCapsule(MakeCapsule({0.0f, 0.0f, 0.0f}), Vector3{3.0f, 0.0f, 0.0f});

    EXPECT_TRUE(h.hit);
    EXPECT_FLOAT_EQ(h.toi, 0.0f);
}

// ProbeGround: 真下の平 AABB を reach 内で拾う
TEST(PhysicsWorldTest, ProbeGroundDetectsAabbBelow)
{
    PhysicsWorld world;
    world.AddAABB(MakeBox({0.0f, 0.0f, 0.0f}, {2.0f, 0.5f, 2.0f}));

    EXPECT_TRUE(world.ProbeGround(Vector3{0.0f, 1.0f, 0.0f}, 0.6f));
}

// ProbeGround: 真下の OBB を reach 内で拾う
TEST(PhysicsWorldTest, ProbeGroundDetectsObbBelow)
{
    PhysicsWorld world;
    world.AddOBB(MakeOBB({0.0f, 0.0f, 0.0f}, Quaternion::Identity, {2.0f, 0.5f, 2.0f}));

    EXPECT_TRUE(world.ProbeGround(Vector3{0.0f, 1.0f, 0.0f}, 0.6f));
}

// ProbeGround: reach 外の床は拾わない
TEST(PhysicsWorldTest, ProbeGroundFalseWhenOutOfReach)
{
    PhysicsWorld world;
    world.AddAABB(MakeBox({0.0f, 0.0f, 0.0f}, {2.0f, 0.5f, 2.0f}));

    EXPECT_FALSE(world.ProbeGround(Vector3{0.0f, 5.0f, 0.0f}, 0.6f));
}

// --- RaycastDown: 接地影の受け先探し ---

namespace
{
    // y = height の水平な床を三角形 2 枚で作る。 Triangle channel が床として見られるかの確認用
    std::array<NS::Physics::Triangle, 2> MakeFlatTriangleFloor(float height, float half = 2.0f) noexcept
    {
        const Vector3 a{-half, height, -half};
        const Vector3 b{-half, height, half};
        const Vector3 c{half, height, half};
        const Vector3 d{half, height, -half};
        return {NS::Physics::Triangle{a, b, c}, NS::Physics::Triangle{a, c, d}};
    }
} // namespace

// 真下の最も近い AABB の上面までの距離を返す
TEST(PhysicsWorldTest, RaycastDownFindsNearestAABB)
{
    PhysicsWorld world;
    world.AddAABB(MakeBox({0.0f, -2.0f, 0.0f}, {0.5f, 0.5f, 0.5f})); // 上面 y=-1.5
    world.AddAABB(MakeBox({0.0f, -5.0f, 0.0f}, {0.5f, 0.5f, 0.5f})); // より遠い
    world.BuildBroadphase();

    float dist = 0.0f;
    EXPECT_TRUE(world.RaycastDown({0.0f, 1.0f, 0.0f}, 12.0f, dist));
    EXPECT_NEAR(dist, 2.5f, 0.001f);
}

// 真下に無ければ当たらない
TEST(PhysicsWorldTest, RaycastDownMissesWhenNothingBelow)
{
    PhysicsWorld world;
    world.AddAABB(MakeBox({10.0f, -2.0f, 0.0f}, {0.5f, 0.5f, 0.5f})); // 横へずれている
    world.BuildBroadphase();

    float dist = -1.0f;
    EXPECT_FALSE(world.RaycastDown({0.0f, 1.0f, 0.0f}, 12.0f, dist));
}

// maxDist より遠い床は無いものとして扱う
TEST(PhysicsWorldTest, RaycastDownRespectsMaxDist)
{
    PhysicsWorld world;
    world.AddAABB(MakeBox({0.0f, -20.0f, 0.0f}, {0.5f, 0.5f, 0.5f})); // 距離 20.5
    world.BuildBroadphase();

    float dist = -1.0f;
    EXPECT_FALSE(world.RaycastDown({0.0f, 1.0f, 0.0f}, 12.0f, dist));
}

// 斜面や自由形状の Triangle channel も床として見る
TEST(PhysicsWorldTest, RaycastDownHitsTriangle)
{
    PhysicsWorld world;
    for (const NS::Physics::Triangle& tri : MakeFlatTriangleFloor(-2.0f))
        world.AddTriangle(tri);
    world.BuildBroadphase();

    float dist = 0.0f;
    EXPECT_TRUE(world.RaycastDown({0.0f, 1.0f, 0.0f}, 12.0f, dist));
    EXPECT_NEAR(dist, 3.0f, 0.001f);
}

// 回転した箱 (OBB channel) も床として見る
TEST(PhysicsWorldTest, RaycastDownHitsOBB)
{
    PhysicsWorld world;
    world.AddOBB(MakeOBB({0.0f, -2.0f, 0.0f}, Quaternion::Identity, {1.0f, 0.5f, 1.0f})); // 上面 y=-1.5
    world.BuildBroadphase();

    float dist = 0.0f;
    EXPECT_TRUE(world.RaycastDown({0.0f, 1.0f, 0.0f}, 12.0f, dist));
    EXPECT_NEAR(dist, 2.5f, 0.001f);
}

// 球とカプセルは立てる床ではないので受け先にしない。 ProbeGround の床の定義と揃える
TEST(PhysicsWorldTest, RaycastDownIgnoresSphereAndCapsule)
{
    PhysicsWorld world;
    Sphere sphere;
    sphere.center = Vector3{0.0f, -2.0f, 0.0f};
    sphere.radius = 1.0f;
    world.AddSphere(sphere);
    world.AddCapsule(MakeCapsule({0.0f, -4.0f, 0.0f}));
    world.BuildBroadphase();

    float dist = -1.0f;
    EXPECT_FALSE(world.RaycastDown({0.0f, 1.0f, 0.0f}, 12.0f, dist));
}

// 最も近い床が channel をまたいでも最近傍を返す
TEST(PhysicsWorldTest, RaycastDownPicksNearestAcrossChannels)
{
    PhysicsWorld world;
    world.AddAABB(MakeBox({0.0f, -8.0f, 0.0f}, {0.5f, 0.5f, 0.5f})); // 上面 y=-7.5、 距離 8.5
    for (const NS::Physics::Triangle& tri : MakeFlatTriangleFloor(-2.0f))
        world.AddTriangle(tri); // 距離 3.0
    world.BuildBroadphase();

    float dist = 0.0f;
    EXPECT_TRUE(world.RaycastDown({0.0f, 1.0f, 0.0f}, 12.0f, dist));
    EXPECT_NEAR(dist, 3.0f, 0.001f);
}
