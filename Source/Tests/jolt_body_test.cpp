#include <Runtime/Core/AABB.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Core/OBB.h>
#include <Runtime/Core/Sphere.h>
#include <Runtime/Physics/PhysicsScene.h>

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace
{
    using NS::Core::OBB;
    using NS::Core::Sphere;
    using NS::Core::Vector3;
    using NS::Phys::PhysicsScene;
    using NS::Phys::Capsule;
    using NS::Phys::Triangle;
    namespace ObjectLayers = NS::Phys::ObjectLayers;

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
    PhysicsScene physics;

    EXPECT_EQ(physics.BodyCount(), 0u);
}

TEST(JoltBody, AddBoxCreatesOneBody)
{
    PhysicsScene physics;

    const JPH::BodyID id =
        physics.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f), ObjectLayers::Terrain);

    EXPECT_FALSE(id.IsInvalid());
    EXPECT_EQ(physics.BodyCount(), 1u);
}

TEST(JoltBody, AddSphereCreatesOneBody)
{
    PhysicsScene physics;
    Sphere sphere;
    sphere.center = Vector3{2.0f, 3.0f, 4.0f};
    sphere.radius = 0.5f;

    const JPH::BodyID id = physics.AddSphere(sphere, ObjectLayers::Rock);

    EXPECT_FALSE(id.IsInvalid());
    EXPECT_EQ(physics.BodyCount(), 1u);
}

TEST(JoltBody, AddCapsuleCreatesOneBody)
{
    PhysicsScene physics;
    const JPH::BodyID id =
        physics.AddCapsule(Capsule{Vector3{0.0f, 2.0f, 0.0f}, Vector3::UnitY, 0.5f, 0.4f}, ObjectLayers::Rock);

    EXPECT_FALSE(id.IsInvalid());
    EXPECT_EQ(physics.BodyCount(), 1u);
}

TEST(JoltBody, AddMeshCreatesOneBodyForManyTriangles)
{
    PhysicsScene physics;

    const JPH::BodyID id = physics.AddMesh(MakeFloorQuad(), ObjectLayers::Terrain);

    EXPECT_FALSE(id.IsInvalid());
    EXPECT_EQ(physics.BodyCount(), 1u);
}

TEST(JoltBody, EmptyMeshCreatesNoBody)
{
    PhysicsScene physics;

    const JPH::BodyID id = physics.AddMesh(std::vector<Triangle>{}, ObjectLayers::Terrain);

    EXPECT_TRUE(id.IsInvalid());
    EXPECT_EQ(physics.BodyCount(), 0u);
}

TEST(JoltBody, BoxKeepsCenter)
{
    PhysicsScene physics;
    const Vector3 center{3.0f, 5.0f, -2.0f};

    const JPH::BodyID id = physics.AddBox(MakeAxisAlignedBox(center, 1.0f, 2.0f, 3.0f), ObjectLayers::Terrain);

    const Vector3 stored = physics.BodyPosition(id);
    EXPECT_NEAR(stored.x, center.x, 1.0e-5f);
    EXPECT_NEAR(stored.y, center.y, 1.0e-5f);
    EXPECT_NEAR(stored.z, center.z, 1.0e-5f);
}

TEST(JoltBody, YawedBoxKeepsItsAxes)
{
    PhysicsScene physics;
    const float yaw = 0.6f;

    const JPH::BodyID id = physics.AddBox(MakeYawedBox(Vector3{0.0f, 0.0f, 0.0f}, yaw), ObjectLayers::Terrain);

    const Vector3 rotatedX = Vector3::Transform(Vector3{1.0f, 0.0f, 0.0f}, physics.BodyRotation(id));
    EXPECT_NEAR(rotatedX.x, std::cos(yaw), 1.0e-5f);
    EXPECT_NEAR(rotatedX.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(rotatedX.z, -std::sin(yaw), 1.0e-5f);
}

namespace
{
    constexpr float k_FixedDelta = 1.0f / 60.0f;

    void Step(PhysicsScene& physics, int steps)
    {
        for (int i = 0; i < steps; ++i)
            physics.Update(k_FixedDelta);
    }
} // namespace

TEST(JoltStep, StaticBodyStaysWhereItWasPut)
{
    PhysicsScene physics;
    const JPH::BodyID id = physics.AddSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, ObjectLayers::Rock);
    physics.OptimizeBroadPhase();

    Step(physics, 30);

    EXPECT_NEAR(physics.BodyPosition(id).y, 10.0f, 1.0e-5f);
}

TEST(JoltStep, DynamicBodyFalls)
{
    PhysicsScene physics;
    const JPH::BodyID id =
        physics.AddDynamicSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, NS::Phys::DynamicBodyDesc{});
    physics.OptimizeBroadPhase();

    Step(physics, 30);

    EXPECT_LT(physics.BodyPosition(id).y, 9.0f);
}

TEST(JoltStep, StaticBodyOfTheSameShapeStaysPut)
{
    PhysicsScene physics;
    const JPH::BodyID id = physics.AddSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, ObjectLayers::Rock);
    physics.OptimizeBroadPhase();

    Step(physics, 30);

    EXPECT_NEAR(physics.BodyPosition(id).y, 10.0f, 1.0e-5f);
}

// 落ちるかは shape と作り方だけで決まる。ObjectLayer は見ていない
TEST(JoltStep, TerrainLayerDynamicBodyFalls)
{
    PhysicsScene physics;
    NS::Phys::DynamicBodyDesc desc;
    desc.layer = ObjectLayers::Terrain;
    const JPH::BodyID id = physics.AddDynamicSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, desc);
    physics.OptimizeBroadPhase();

    Step(physics, 30);

    EXPECT_LT(physics.BodyPosition(id).y, 9.0f);
}

TEST(JoltStep, MeshBodyStaysStatic)
{
    PhysicsScene physics;
    const std::vector<Triangle> floor = MakeFloorQuad();
    const JPH::BodyID id = physics.AddMesh(floor, ObjectLayers::Terrain);
    physics.OptimizeBroadPhase();

    Step(physics, 30);

    EXPECT_NEAR(physics.BodyPosition(id).y, 0.0f, 1.0e-5f);
}

TEST(JoltStep, LinearVelocityCarriesTheBody)
{
    PhysicsScene physics;
    const JPH::BodyID id =
        physics.AddDynamicSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, NS::Phys::DynamicBodyDesc{});
    physics.OptimizeBroadPhase();

    physics.SetBodyVelocity(id, Vector3{5.0f, 0.0f, 0.0f});
    Step(physics, 30);

    EXPECT_GT(physics.BodyPosition(id).x, 1.0f);
    EXPECT_GT(physics.BodyVelocity(id).x, 0.0f);
}

TEST(JoltStep, VelocityOfStaticBodyIsZero)
{
    PhysicsScene physics;
    const JPH::BodyID id = physics.AddSphere(Sphere{Vector3{0.0f, 10.0f, 0.0f}, 1.0f}, ObjectLayers::Rock);
    physics.OptimizeBroadPhase();

    Step(physics, 30);

    EXPECT_NEAR(physics.BodyVelocity(id).y, 0.0f, 1.0e-5f);
}

TEST(JoltQuery, RaycastDownwardFindsTheFloorBelow)
{
    PhysicsScene physics;
    physics.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 4.0f, 0.5f, 4.0f), ObjectLayers::Terrain);
    physics.OptimizeBroadPhase();

    float distance = 0.0f;
    const bool found = physics.Raycast(Vector3{0.0f, 3.0f, 0.0f}, Vector3{0.0f, -1.0f, 0.0f}, 64.0f, distance);

    EXPECT_TRUE(found);
    EXPECT_NEAR(distance, 2.5f, 1.0e-3f);
}

TEST(JoltQuery, RaycastDownwardMissesWhenNothingIsBelow)
{
    PhysicsScene physics;
    physics.AddBox(MakeAxisAlignedBox(Vector3{20.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f), ObjectLayers::Terrain);
    physics.OptimizeBroadPhase();

    float distance = 0.0f;
    const bool found = physics.Raycast(Vector3{0.0f, 3.0f, 0.0f}, Vector3{0.0f, -1.0f, 0.0f}, 64.0f, distance);

    EXPECT_FALSE(found);
}

TEST(JoltQuery, RaycastStopsAtMaxDistance)
{
    PhysicsScene physics;
    physics.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 4.0f, 0.5f, 4.0f), ObjectLayers::Terrain);
    physics.OptimizeBroadPhase();

    float distance = 0.0f;
    const bool found = physics.Raycast(Vector3{0.0f, 3.0f, 0.0f}, Vector3{0.0f, -1.0f, 0.0f}, 1.0f, distance);

    EXPECT_FALSE(found);
}

// 向き (6, 8, 24) は長さ 26、起点から球の中心までは (3, 4, 12) で 13。半径 1 なので当たりまで 12
// 長さを 1 にそろえずに渡すと距離が 26 分の 1 に縮み、成分を取り違えると球を外れる
TEST(JoltQuery, RaycastHitsAlongAnObliqueDirection)
{
    PhysicsScene physics;
    physics.AddSphere(Sphere{Vector3{4.0f, 2.0f, 12.5f}, 1.0f}, ObjectLayers::Rock);
    physics.OptimizeBroadPhase();

    float distance = 0.0f;
    const bool found = physics.Raycast(Vector3{1.0f, -2.0f, 0.5f}, Vector3{6.0f, 8.0f, 24.0f}, 64.0f, distance);

    EXPECT_TRUE(found);
    EXPECT_NEAR(distance, 12.0f, 1.0e-3f);
}

TEST(JoltQuery, OverlapBoxReturnsTheBoundsOfTouchingBodies)
{
    PhysicsScene physics;
    physics.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f), ObjectLayers::Terrain);
    physics.AddBox(MakeAxisAlignedBox(Vector3{10.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f), ObjectLayers::Terrain);
    physics.OptimizeBroadPhase();

    NS::Core::AABB region;
    region.Center = NS::Core::Vector3{0.5f, 0.0f, 0.0f};
    region.Extents = NS::Core::Vector3{0.25f, 0.25f, 0.25f};
    const std::vector<NS::Core::AABB> found = physics.OverlapBox(region);

    ASSERT_EQ(found.size(), std::size_t{1});
    EXPECT_NEAR(found[0].Center.x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(found[0].Extents.y, 1.0f, 1.0e-3f);
}

TEST(JoltQuery, OverlapBoxReturnsNothingInEmptySpace)
{
    PhysicsScene physics;
    physics.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f), ObjectLayers::Terrain);
    physics.OptimizeBroadPhase();

    NS::Core::AABB region;
    region.Center = NS::Core::Vector3{20.0f, 0.0f, 0.0f};
    region.Extents = NS::Core::Vector3{0.5f, 0.5f, 0.5f};

    EXPECT_TRUE(physics.OverlapBox(region).empty());
}

namespace
{
    using NS::Phys::BodyContact;
    using NS::Phys::DynamicBodyDesc;

    void AddWideFloor(PhysicsScene& physics)
    {
        physics.AddBox(MakeAxisAlignedBox(Vector3{0.0f, -0.5f, 0.0f}, 20.0f, 0.5f, 20.0f), ObjectLayers::Terrain);
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
    PhysicsScene physics;
    AddWideFloor(physics);
    const JPH::BodyID rock = physics.AddDynamicSphere(MakeSphere(Vector3{0.0f, 4.0f, 0.0f}, 0.5f), DynamicBodyDesc{});
    physics.OptimizeBroadPhase();

    Step(physics, 180);

    EXPECT_NEAR(physics.BodyPosition(rock).y, 0.5f, 0.05f);
}

// 反発を上げた球は落ちた後に一度浮き上がる。跳ねない実装でも接地の高さは同じなので、上向きの速度で見る
TEST(JoltDynamic, RestitutionMakesTheSphereBounceBackUp)
{
    PhysicsScene physics;
    AddWideFloor(physics);
    DynamicBodyDesc bouncy;
    bouncy.restitution = 0.8f;
    const JPH::BodyID rock = physics.AddDynamicSphere(MakeSphere(Vector3{0.0f, 4.0f, 0.0f}, 0.5f), bouncy);
    physics.OptimizeBroadPhase();

    float peakUpward = 0.0f;
    for (int i = 0; i < 120; ++i)
    {
        physics.Update(1.0f / 60.0f);
        peakUpward = std::max(peakUpward, physics.BodyVelocity(rock).y);
    }

    EXPECT_GT(peakUpward, 3.0f);
}

TEST(JoltDynamic, HorizontalPushMakesTheSphereSpin)
{
    PhysicsScene physics;
    AddWideFloor(physics);
    const JPH::BodyID rock = physics.AddDynamicSphere(MakeSphere(Vector3{0.0f, 0.5f, 0.0f}, 0.5f), DynamicBodyDesc{});
    physics.OptimizeBroadPhase();
    physics.SetBodyVelocity(rock, Vector3{8.0f, 0.0f, 0.0f});

    Step(physics, 30);

    // X+ へ転がる球は Z 軸の負まわりに回る
    EXPECT_LT(physics.BodyAngularVelocity(rock).z, -1.0f);
}

TEST(JoltDynamic, LandingReportsAnUpwardContactNormal)
{
    PhysicsScene physics;
    AddWideFloor(physics);
    const JPH::BodyID rock = physics.AddDynamicSphere(MakeSphere(Vector3{0.0f, 2.0f, 0.0f}, 0.5f), DynamicBodyDesc{});
    physics.OptimizeBroadPhase();

    std::vector<BodyContact> landing;
    for (int i = 0; i < 120 && landing.empty(); ++i)
    {
        physics.Update(1.0f / 60.0f);
        landing = physics.ContactsOf(rock);
    }

    ASSERT_FALSE(landing.empty());
    EXPECT_GT(landing[0].normal.y, 0.9f);
}

TEST(JoltDynamic, HittingAWallReportsASidewaysContactNormal)
{
    PhysicsScene physics;
    AddWideFloor(physics);
    physics.AddBox(MakeAxisAlignedBox(Vector3{4.0f, 2.0f, 0.0f}, 0.5f, 2.0f, 4.0f), ObjectLayers::Terrain);
    const JPH::BodyID rock = physics.AddDynamicSphere(MakeSphere(Vector3{0.0f, 1.5f, 0.0f}, 0.5f), DynamicBodyDesc{});
    physics.OptimizeBroadPhase();
    physics.SetBodyVelocity(rock, Vector3{25.0f, 0.0f, 0.0f});

    float sidewaysNormalY = 1.0f;
    for (int i = 0; i < 60; ++i)
    {
        physics.Update(1.0f / 60.0f);
        for (const BodyContact& contact : physics.ContactsOf(rock))
            sidewaysNormalY = std::min(sidewaysNormalY, contact.normal.y);
    }

    EXPECT_LT(sidewaysNormalY, 0.7f);
}

// 集めるのは新しく起きた接触だけ。持ち越すと、床に載ったままの岩が毎フレームぶつかり直しているように見える
TEST(JoltDynamic, ContactsCoverOnlyTheNewTouchesOfTheLatestStep)
{
    PhysicsScene physics;
    AddWideFloor(physics);
    const JPH::BodyID rock = physics.AddDynamicSphere(MakeSphere(Vector3{0.0f, 0.6f, 0.0f}, 0.5f), DynamicBodyDesc{});
    physics.OptimizeBroadPhase();

    int landingStep = -1;
    for (int i = 0; i < 60 && landingStep < 0; ++i)
    {
        Step(physics, 1);
        if (!physics.ContactsOf(rock).empty())
            landingStep = i;
    }
    ASSERT_GE(landingStep, 0);

    Step(physics, 1);

    EXPECT_TRUE(physics.ContactsOf(rock).empty());
}

TEST(JoltDynamic, RestingBodyFallsAsleep)
{
    PhysicsScene physics;
    AddWideFloor(physics);
    const JPH::BodyID rock = physics.AddDynamicSphere(MakeSphere(Vector3{0.0f, 0.5f, 0.0f}, 0.5f), DynamicBodyDesc{});
    physics.OptimizeBroadPhase();

    Step(physics, 5);
    const bool awakeAtFirst = physics.IsBodyAwake(rock);
    Step(physics, 240);

    EXPECT_TRUE(awakeAtFirst);
    EXPECT_FALSE(physics.IsBodyAwake(rock));
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
    PhysicsScene physics;
    const JPH::BodyID box =
        physics.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 0.5f, 0.5f, 0.5f), ObjectLayers::Terrain);
    physics.OptimizeBroadPhase();

    const std::vector<JPH::BodyID> hit =
        physics.OverlapCapsule(Capsule{Vector3{0.8f, 0.0f, 0.0f}, Vector3::UnitY, 0.5f, 0.4f});

    EXPECT_TRUE(Contains(hit, box));
}

TEST(JoltOverlap, CapsuleOutOfReachFindsNothing)
{
    PhysicsScene physics;
    physics.AddBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 0.5f, 0.5f, 0.5f), ObjectLayers::Terrain);
    physics.OptimizeBroadPhase();

    EXPECT_TRUE(physics.OverlapCapsule(Capsule{Vector3{3.0f, 0.0f, 0.0f}, Vector3::UnitY, 0.5f, 0.4f}).empty());
}

// 外接箱で見ると、45 度傾いた箱の角の外側でも当たったことになる
TEST(JoltOverlap, RotatedBoxIsJudgedByItsRealShape)
{
    PhysicsScene physics;
    NS::Core::OBB box = NS::Core::MakeOBB(Vector3{0.0f, 0.0f, 0.0f},
                                          NS::Core::EulerDegreesToQuaternion(Vector3{0.0f, 45.0f, 0.0f}),
                                          Vector3{0.5f, 0.5f, 0.5f});
    const JPH::BodyID id = physics.AddBox(box, ObjectLayers::Terrain);
    physics.OptimizeBroadPhase();

    // 外接箱の角。実物の面までは 0.5 あるので半径 0.4 の capsule は届かない
    const float corner = 0.5f * 1.41421356f;
    EXPECT_FALSE(
        Contains(physics.OverlapCapsule(Capsule{Vector3{corner, 0.0f, corner}, Vector3::UnitY, 0.5f, 0.4f}), id));
    // 面の正面からは当たる
    EXPECT_TRUE(Contains(physics.OverlapCapsule(Capsule{Vector3{0.3f, 0.0f, 0.3f}, Vector3::UnitY, 0.5f, 0.4f}), id));
}

TEST(JoltOverlap, SensorBoxIsFoundLikeAnyOtherBody)
{
    PhysicsScene physics;
    const JPH::BodyID sensor = physics.AddSensorBox(MakeAxisAlignedBox(Vector3{0.0f, 0.0f, 0.0f}, 0.5f, 0.5f, 0.5f));
    physics.OptimizeBroadPhase();

    EXPECT_FALSE(sensor.IsInvalid());
    EXPECT_TRUE(
        Contains(physics.OverlapCapsule(Capsule{Vector3{0.0f, 0.0f, 0.0f}, Vector3::UnitY, 0.5f, 0.4f}), sensor));
}

// 同じ勢いで突かれても重い物ほど動かない。飛距離が重さの表示になる
TEST(JoltDynamic, HeavierTargetIsPushedLessByTheSameHit)
{
    const auto pushedDistance = [](float targetMass) {
        PhysicsScene physics;
        AddWideFloor(physics);
        DynamicBodyDesc hitter;
        const JPH::BodyID moving = physics.AddDynamicSphere(MakeSphere(Vector3{-2.0f, 0.5f, 0.0f}, 0.5f), hitter);
        DynamicBodyDesc target;
        target.mass = targetMass;
        const JPH::BodyID hit = physics.AddDynamicSphere(MakeSphere(Vector3{0.0f, 0.5f, 0.0f}, 0.5f), target);
        physics.OptimizeBroadPhase();
        physics.SetBodyVelocity(moving, Vector3{20.0f, 0.0f, 0.0f});
        Step(physics, 60);
        return physics.BodyPosition(hit).x;
    };

    EXPECT_LT(pushedDistance(8.0f), pushedDistance(1.0f));
}

TEST(JoltDynamic, BoxCanBeMadeDynamicToo)
{
    PhysicsScene physics;
    AddWideFloor(physics);
    const JPH::BodyID rock =
        physics.AddDynamicBox(MakeAxisAlignedBox(Vector3{0.0f, 4.0f, 0.0f}, 0.5f, 0.5f, 0.5f), DynamicBodyDesc{});
    physics.OptimizeBroadPhase();

    Step(physics, 180);

    EXPECT_NEAR(physics.BodyPosition(rock).y, 0.5f, 0.05f);
}
