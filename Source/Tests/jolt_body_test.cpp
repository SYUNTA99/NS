#include <Runtime/Core/Math.h>
#include <Runtime/Physics/PhysicsWorld.h>

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace
{
    using NS::Core::OBB;
    using NS::Core::Sphere;
    using NS::Core::Vector3;
    using NS::Physics::PhysicsWorld;
    using NS::Physics::Capsule;
    using NS::Physics::Triangle;
    namespace ObjectLayers = NS::Physics::ObjectLayers;

    OBB MakeAxisAlignedBox(const Vector3& center, float halfX, float halfY, float halfZ)
    {
        OBB box;
        box.center = center;
        box.halfExtentX = halfX;
        box.halfExtentY = halfY;
        box.halfExtentZ = halfZ;
        return box;
    }

    OBB MakeYawedBox(const Vector3& center, float yawRadians)
    {
        OBB box = MakeAxisAlignedBox(center, 1.0f, 2.0f, 3.0f);
        const float cosYaw = std::cos(yawRadians);
        const float sinYaw = std::sin(yawRadians);
        box.axisX = Vector3{cosYaw, 0.0f, -sinYaw};
        box.axisZ = Vector3{sinYaw, 0.0f, cosYaw};
        return box;
    }

    std::vector<Triangle> MakeFloorQuad()
    {
        return {
            Triangle{Vector3{-1.0f, 0.0f, -1.0f}, Vector3{1.0f, 0.0f, 1.0f}, Vector3{1.0f, 0.0f, -1.0f}},
            Triangle{Vector3{-1.0f, 0.0f, -1.0f}, Vector3{-1.0f, 0.0f, 1.0f}, Vector3{1.0f, 0.0f, 1.0f}},
        };
    }
} // namespace

TEST(JoltBody, StartsEmpty)
{
    PhysicsWorld world;

    EXPECT_EQ(world.BodyCount(), 0u);
}

TEST(JoltBody, AddBoxCreatesOneBody)
{
    PhysicsWorld world;

    const JPH::BodyID id =
        world.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f), ObjectLayers::Terrain);

    EXPECT_FALSE(id.IsInvalid());
    EXPECT_EQ(world.BodyCount(), 1u);
}

TEST(JoltBody, AddSphereCreatesOneBody)
{
    PhysicsWorld world;
    Sphere sphere;
    sphere.center = Vector3{2.0f, 3.0f, 4.0f};
    sphere.radius = 0.5f;

    const JPH::BodyID id = world.AddSphere(sphere, ObjectLayers::Rock);

    EXPECT_FALSE(id.IsInvalid());
    EXPECT_EQ(world.BodyCount(), 1u);
}

TEST(JoltBody, AddCapsuleCreatesOneBody)
{
    PhysicsWorld world;
    const JPH::BodyID id =
        world.AddCapsule(Capsule{Vector3{0.0f, 2.0f, 0.0f}, Vector3::UnitY, 0.5f, 0.4f}, ObjectLayers::Rock);

    EXPECT_FALSE(id.IsInvalid());
    EXPECT_EQ(world.BodyCount(), 1u);
}

TEST(JoltBody, AddMeshCreatesOneBodyForManyTriangles)
{
    PhysicsWorld world;

    const JPH::BodyID id = world.AddMesh(MakeFloorQuad(), ObjectLayers::Terrain);

    EXPECT_FALSE(id.IsInvalid());
    EXPECT_EQ(world.BodyCount(), 1u);
}

TEST(JoltBody, EmptyMeshCreatesNoBody)
{
    PhysicsWorld world;

    const JPH::BodyID id = world.AddMesh(std::vector<Triangle>{}, ObjectLayers::Terrain);

    EXPECT_TRUE(id.IsInvalid());
    EXPECT_EQ(world.BodyCount(), 0u);
}

TEST(JoltBody, BoxKeepsCenter)
{
    PhysicsWorld world;
    const Vector3 center{3.0f, 5.0f, -2.0f};

    const JPH::BodyID id = world.AddBox(MakeAxisAlignedBox(center, 1.0f, 2.0f, 3.0f), ObjectLayers::Terrain);

    const Vector3 stored = world.BodyPosition(id);
    EXPECT_NEAR(stored.x, center.x, 1.0e-5f);
    EXPECT_NEAR(stored.y, center.y, 1.0e-5f);
    EXPECT_NEAR(stored.z, center.z, 1.0e-5f);
}

TEST(JoltBody, YawedBoxKeepsItsAxes)
{
    PhysicsWorld world;
    const float yaw = 0.6f;

    const JPH::BodyID id = world.AddBox(MakeYawedBox(Vector3{0.0f, 0.0f, 0.0f}, yaw), ObjectLayers::Terrain);

    const Vector3 rotatedX = Vector3::Transform(Vector3{1.0f, 0.0f, 0.0f}, world.BodyRotation(id));
    EXPECT_NEAR(rotatedX.x, std::cos(yaw), 1.0e-5f);
    EXPECT_NEAR(rotatedX.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(rotatedX.z, -std::sin(yaw), 1.0e-5f);
}

namespace
{
    constexpr float k_FixedDelta = 1.0f / 60.0f;

    void Step(PhysicsWorld& world, int steps)
    {
        for (int i = 0; i < steps; ++i)
            world.Update(k_FixedDelta);
    }
} // namespace

TEST(JoltStep, StaticBodyStaysWhereItWasPut)
{
    PhysicsWorld world;
    const JPH::BodyID id = world.AddSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, ObjectLayers::Rock);
    world.OptimizeBroadPhase();

    Step(world, 30);

    EXPECT_NEAR(world.BodyPosition(id).y, 10.0f, 1.0e-5f);
}

TEST(JoltStep, DynamicBodyFalls)
{
    PhysicsWorld world;
    const JPH::BodyID id = world.AddSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, ObjectLayers::Rock);
    world.OptimizeBroadPhase();
    world.SetBodyDynamic(id, true);

    Step(world, 30);

    EXPECT_LT(world.BodyPosition(id).y, 9.0f);
}

TEST(JoltStep, BodyTurnedBackToStaticStopsFalling)
{
    PhysicsWorld world;
    const JPH::BodyID id = world.AddSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, ObjectLayers::Rock);
    world.OptimizeBroadPhase();
    world.SetBodyDynamic(id, true);
    Step(world, 30);

    world.SetBodyDynamic(id, false);
    const float restingY = world.BodyPosition(id).y;
    Step(world, 30);

    EXPECT_NEAR(world.BodyPosition(id).y, restingY, 1.0e-5f);
}

// dynamic にできるかは shape だけで決まる。ObjectLayer は見ていない
TEST(JoltStep, TerrainBodyCanBecomeDynamic)
{
    PhysicsWorld world;
    const JPH::BodyID id = world.AddSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, ObjectLayers::Terrain);
    world.OptimizeBroadPhase();
    world.SetBodyDynamic(id, true);

    Step(world, 30);

    EXPECT_LT(world.BodyPosition(id).y, 9.0f);
}

// MeshShape::MustBeStatic が true なので SetBodyDynamic は警告を出して戻る
TEST(JoltStep, MeshBodyStaysStatic)
{
    PhysicsWorld world;
    const std::vector<Triangle> floor = MakeFloorQuad();
    const JPH::BodyID id = world.AddMesh(floor, ObjectLayers::Terrain);
    world.OptimizeBroadPhase();

    world.SetBodyDynamic(id, true);
    Step(world, 30);

    EXPECT_NEAR(world.BodyPosition(id).y, 0.0f, 1.0e-5f);
}

TEST(JoltStep, LinearVelocityCarriesTheBody)
{
    PhysicsWorld world;
    const JPH::BodyID id = world.AddSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, ObjectLayers::Rock);
    world.OptimizeBroadPhase();
    world.SetBodyDynamic(id, true);

    world.SetBodyVelocity(id, Vector3{5.0f, 0.0f, 0.0f});
    Step(world, 30);

    EXPECT_GT(world.BodyPosition(id).x, 1.0f);
    EXPECT_GT(world.BodyVelocity(id).x, 0.0f);
}

TEST(JoltStep, VelocityOfStaticBodyIsZero)
{
    PhysicsWorld world;
    const JPH::BodyID id = world.AddSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, ObjectLayers::Rock);
    world.OptimizeBroadPhase();

    Step(world, 30);

    EXPECT_NEAR(world.BodyVelocity(id).y, 0.0f, 1.0e-5f);
}

TEST(JoltQuery, RaycastDownFindsTheFloorBelow)
{
    PhysicsWorld world;
    world.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 4.0f, 0.5f, 4.0f), ObjectLayers::Terrain);
    world.OptimizeBroadPhase();

    float distance = 0.0f;
    const bool found = world.RaycastDown(Vector3{0.0f, 3.0f, 0.0f}, 64.0f, distance);

    EXPECT_TRUE(found);
    EXPECT_NEAR(distance, 2.5f, 1.0e-3f);
}

TEST(JoltQuery, RaycastDownMissesWhenNothingIsBelow)
{
    PhysicsWorld world;
    world.AddBox(MakeAxisAlignedBox(Vector3{20.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f), ObjectLayers::Terrain);
    world.OptimizeBroadPhase();

    float distance = 0.0f;
    const bool found = world.RaycastDown(Vector3{0.0f, 3.0f, 0.0f}, 64.0f, distance);

    EXPECT_FALSE(found);
}

TEST(JoltQuery, RaycastDownStopsAtMaxDistance)
{
    PhysicsWorld world;
    world.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 4.0f, 0.5f, 4.0f), ObjectLayers::Terrain);
    world.OptimizeBroadPhase();

    float distance = 0.0f;
    const bool found = world.RaycastDown(Vector3{0.0f, 3.0f, 0.0f}, 1.0f, distance);

    EXPECT_FALSE(found);
}

TEST(JoltQuery, OverlapBoxReturnsTheBoundsOfTouchingBodies)
{
    PhysicsWorld world;
    world.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f), ObjectLayers::Terrain);
    world.AddBox(MakeAxisAlignedBox(Vector3{10.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f), ObjectLayers::Terrain);
    world.OptimizeBroadPhase();

    NS::Core::AABB region;
    region.Center = NS::Core::Vector3{0.5f, 0.0f, 0.0f};
    region.Extents = NS::Core::Vector3{0.25f, 0.25f, 0.25f};
    const std::vector<NS::Core::AABB> found = world.OverlapBox(region);

    ASSERT_EQ(found.size(), std::size_t{1});
    EXPECT_NEAR(found[0].Center.x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(found[0].Extents.y, 1.0f, 1.0e-3f);
}

TEST(JoltQuery, OverlapBoxReturnsNothingInEmptySpace)
{
    PhysicsWorld world;
    world.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f), ObjectLayers::Terrain);
    world.OptimizeBroadPhase();

    NS::Core::AABB region;
    region.Center = NS::Core::Vector3{20.0f, 0.0f, 0.0f};
    region.Extents = NS::Core::Vector3{0.5f, 0.5f, 0.5f};

    EXPECT_TRUE(world.OverlapBox(region).empty());
}

namespace
{
    using NS::Physics::BodyContact;
    using NS::Physics::DynamicBodyDesc;

    void AddWideFloor(PhysicsWorld& world)
    {
        world.AddBox(MakeAxisAlignedBox(Vector3{0.0f, -0.5f, 0.0f}, 20.0f, 0.5f, 20.0f), ObjectLayers::Terrain);
    }

    Sphere MakeSphere(const Vector3& center, float radius)
    {
        Sphere sphere;
        sphere.center = center;
        sphere.radius = radius;
        return sphere;
    }
} // namespace

TEST(JoltDynamic, SphereFallsAndRestsOnTheFloor)
{
    PhysicsWorld world;
    AddWideFloor(world);
    const JPH::BodyID rock = world.AddDynamicSphere(MakeSphere(Vector3{0.0f, 4.0f, 0.0f}, 0.5f), DynamicBodyDesc{});
    world.OptimizeBroadPhase();

    Step(world, 180);

    EXPECT_NEAR(world.BodyPosition(rock).y, 0.5f, 0.05f);
}

// 反発を上げた球は落ちた後に一度浮き上がる。跳ねない実装でも接地の高さは同じなので、上向きの速度で見る
TEST(JoltDynamic, RestitutionMakesTheSphereBounceBackUp)
{
    PhysicsWorld world;
    AddWideFloor(world);
    DynamicBodyDesc bouncy;
    bouncy.restitution = 0.8f;
    const JPH::BodyID rock = world.AddDynamicSphere(MakeSphere(Vector3{0.0f, 4.0f, 0.0f}, 0.5f), bouncy);
    world.OptimizeBroadPhase();

    float peakUpward = 0.0f;
    for (int i = 0; i < 120; ++i)
    {
        world.Update(1.0f / 60.0f);
        peakUpward = std::max(peakUpward, world.BodyVelocity(rock).y);
    }

    EXPECT_GT(peakUpward, 3.0f);
}

TEST(JoltDynamic, HorizontalPushMakesTheSphereSpin)
{
    PhysicsWorld world;
    AddWideFloor(world);
    const JPH::BodyID rock = world.AddDynamicSphere(MakeSphere(Vector3{0.0f, 0.5f, 0.0f}, 0.5f), DynamicBodyDesc{});
    world.OptimizeBroadPhase();
    world.SetBodyVelocity(rock, Vector3{8.0f, 0.0f, 0.0f});

    Step(world, 30);

    // X+ へ転がる球は Z 軸の負まわりに回る
    EXPECT_LT(world.BodyAngularVelocity(rock).z, -1.0f);
}

TEST(JoltDynamic, LandingReportsAnUpwardContactNormal)
{
    PhysicsWorld world;
    AddWideFloor(world);
    const JPH::BodyID rock = world.AddDynamicSphere(MakeSphere(Vector3{0.0f, 2.0f, 0.0f}, 0.5f), DynamicBodyDesc{});
    world.OptimizeBroadPhase();

    std::vector<BodyContact> landing;
    for (int i = 0; i < 120 && landing.empty(); ++i)
    {
        world.Update(1.0f / 60.0f);
        landing = world.ContactsOf(rock);
    }

    ASSERT_FALSE(landing.empty());
    EXPECT_GT(landing[0].normal.y, 0.9f);
}

TEST(JoltDynamic, HittingAWallReportsASidewaysContactNormal)
{
    PhysicsWorld world;
    AddWideFloor(world);
    world.AddBox(MakeAxisAlignedBox(Vector3{4.0f, 2.0f, 0.0f}, 0.5f, 2.0f, 4.0f), ObjectLayers::Terrain);
    const JPH::BodyID rock = world.AddDynamicSphere(MakeSphere(Vector3{0.0f, 1.5f, 0.0f}, 0.5f), DynamicBodyDesc{});
    world.OptimizeBroadPhase();
    world.SetBodyVelocity(rock, Vector3{25.0f, 0.0f, 0.0f});

    float sidewaysNormalY = 1.0f;
    for (int i = 0; i < 60; ++i)
    {
        world.Update(1.0f / 60.0f);
        for (const BodyContact& contact : world.ContactsOf(rock))
            sidewaysNormalY = std::min(sidewaysNormalY, contact.normal.y);
    }

    EXPECT_LT(sidewaysNormalY, 0.7f);
}

// 集めるのは新しく起きた接触だけ。持ち越すと、床に載ったままの岩が毎歩ぶつかり直しているように見える
TEST(JoltDynamic, ContactsCoverOnlyTheNewTouchesOfTheLatestStep)
{
    PhysicsWorld world;
    AddWideFloor(world);
    const JPH::BodyID rock = world.AddDynamicSphere(MakeSphere(Vector3{0.0f, 0.6f, 0.0f}, 0.5f), DynamicBodyDesc{});
    world.OptimizeBroadPhase();

    int landingStep = -1;
    for (int i = 0; i < 60 && landingStep < 0; ++i)
    {
        Step(world, 1);
        if (!world.ContactsOf(rock).empty())
            landingStep = i;
    }
    ASSERT_GE(landingStep, 0);

    Step(world, 1);

    EXPECT_TRUE(world.ContactsOf(rock).empty());
}

TEST(JoltDynamic, RestingBodyFallsAsleep)
{
    PhysicsWorld world;
    AddWideFloor(world);
    const JPH::BodyID rock = world.AddDynamicSphere(MakeSphere(Vector3{0.0f, 0.5f, 0.0f}, 0.5f), DynamicBodyDesc{});
    world.OptimizeBroadPhase();

    Step(world, 5);
    const bool awakeAtFirst = world.IsBodyAwake(rock);
    Step(world, 240);

    EXPECT_TRUE(awakeAtFirst);
    EXPECT_FALSE(world.IsBodyAwake(rock));
}

namespace
{
    bool Contains(const std::vector<JPH::BodyID>& ids, JPH::BodyID id)
    {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }
} // namespace

TEST(JoltOverlap, CapsuleFindsTheBoxItTouches)
{
    PhysicsWorld world;
    const JPH::BodyID box =
        world.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 0.5f, 0.5f, 0.5f), ObjectLayers::Terrain);
    world.OptimizeBroadPhase();

    const std::vector<JPH::BodyID> hit =
        world.OverlapCapsule(Capsule{Vector3{0.8f, 0.0f, 0.0f}, Vector3::UnitY, 0.5f, 0.4f});

    EXPECT_TRUE(Contains(hit, box));
}

TEST(JoltOverlap, CapsuleOutOfReachFindsNothing)
{
    PhysicsWorld world;
    world.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 0.5f, 0.5f, 0.5f), ObjectLayers::Terrain);
    world.OptimizeBroadPhase();

    EXPECT_TRUE(world.OverlapCapsule(Capsule{Vector3{3.0f, 0.0f, 0.0f}, Vector3::UnitY, 0.5f, 0.4f}).empty());
}

// 外接箱で見ると、45 度傾いた箱の角の外側でも当たったことになる
TEST(JoltOverlap, RotatedBoxIsJudgedByItsRealShape)
{
    PhysicsWorld world;
    NS::Core::OBB box = NS::Core::MakeOBB(Vector3{0.0f, 0.0f, 0.0f},
                                          NS::Core::EulerDegreesToQuaternion(Vector3{0.0f, 45.0f, 0.0f}),
                                          Vector3{0.5f, 0.5f, 0.5f});
    const JPH::BodyID id = world.AddBox(box, ObjectLayers::Terrain);
    world.OptimizeBroadPhase();

    // 外接箱の角。実物の面までは 0.5 あるので半径 0.4 の capsule は届かない
    const float corner = 0.5f * 1.41421356f;
    EXPECT_FALSE(
        Contains(world.OverlapCapsule(Capsule{Vector3{corner, 0.0f, corner}, Vector3::UnitY, 0.5f, 0.4f}), id));
    // 面の正面からは当たる
    EXPECT_TRUE(Contains(world.OverlapCapsule(Capsule{Vector3{0.3f, 0.0f, 0.3f}, Vector3::UnitY, 0.5f, 0.4f}), id));
}

TEST(JoltOverlap, SensorBoxIsFoundLikeAnyOtherBody)
{
    PhysicsWorld world;
    const JPH::BodyID sensor = world.AddSensorBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 0.5f, 0.5f, 0.5f));
    world.OptimizeBroadPhase();

    EXPECT_FALSE(sensor.IsInvalid());
    EXPECT_TRUE(Contains(world.OverlapCapsule(Capsule{Vector3{0.0f, 0.0f, 0.0f}, Vector3::UnitY, 0.5f, 0.4f}), sensor));
}

// 同じ勢いで突かれても重い物ほど動かない。飛距離が重さの表示になる
TEST(JoltDynamic, HeavierTargetIsPushedLessByTheSameHit)
{
    const auto pushedDistance = [](float targetMass) {
        PhysicsWorld world;
        AddWideFloor(world);
        DynamicBodyDesc hitter;
        const JPH::BodyID moving = world.AddDynamicSphere(MakeSphere(Vector3{-2.0f, 0.5f, 0.0f}, 0.5f), hitter);
        DynamicBodyDesc target;
        target.mass = targetMass;
        const JPH::BodyID hit = world.AddDynamicSphere(MakeSphere(Vector3{0.0f, 0.5f, 0.0f}, 0.5f), target);
        world.OptimizeBroadPhase();
        world.SetBodyVelocity(moving, Vector3{20.0f, 0.0f, 0.0f});
        Step(world, 60);
        return world.BodyPosition(hit).x;
    };

    EXPECT_LT(pushedDistance(8.0f), pushedDistance(1.0f));
}

TEST(JoltDynamic, BoxCanBeMadeDynamicToo)
{
    PhysicsWorld world;
    AddWideFloor(world);
    const JPH::BodyID rock =
        world.AddDynamicBox(MakeAxisAlignedBox(Vector3{0.0f, 4.0f, 0.0f}, 0.5f, 0.5f, 0.5f), DynamicBodyDesc{});
    world.OptimizeBroadPhase();

    Step(world, 180);

    EXPECT_NEAR(world.BodyPosition(rock).y, 0.5f, 0.05f);
}
