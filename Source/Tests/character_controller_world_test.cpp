#include <gtest/gtest.h>

#include <Framework/Math/Math.h>
#include <Framework/Physics/CharacterController.h>
#include <Framework/Physics/PhysicsWorld.h>

#include <array>
#include <span>

namespace
{
    using NS::Math::AABB;
    using NS::Math::Quaternion;
    using NS::Math::Vector3;
    using NS::Physics::CharacterController;
    using NS::Physics::CharacterControllerInput;
    using NS::Physics::CharacterControllerResult;
    using NS::Physics::MakeObb;
    using NS::Physics::OBB;
    using NS::Physics::PhysicsWorld;
    using NS::Physics::Sphere;

    constexpr float kPi = 3.14159265358979323846f;

    AABB MakeBox(const Vector3& center, const Vector3& extents)
    {
        AABB b;
        b.Center = center;
        b.Extents = extents;
        return b;
    }

    // span path (旧) と world path (新) の結果が完全一致することを確認する
    // 両方とも grid 無し (span 側 grid=null / world 側 BuildBroadphase 未呼出) で総当たり同条件にする
    void ExpectResultsEqual(const CharacterControllerResult& a, const CharacterControllerResult& b)
    {
        EXPECT_EQ(a.grounded, b.grounded);
        EXPECT_FLOAT_EQ(a.position.x, b.position.x);
        EXPECT_FLOAT_EQ(a.position.y, b.position.y);
        EXPECT_FLOAT_EQ(a.position.z, b.position.z);
        EXPECT_FLOAT_EQ(a.velocity.x, b.velocity.x);
        EXPECT_FLOAT_EQ(a.velocity.y, b.velocity.y);
        EXPECT_FLOAT_EQ(a.velocity.z, b.velocity.z);
        EXPECT_FLOAT_EQ(a.contactNormal.x, b.contactNormal.x);
        EXPECT_FLOAT_EQ(a.contactNormal.y, b.contactNormal.y);
        EXPECT_FLOAT_EQ(a.contactNormal.z, b.contactNormal.z);
    }
} // namespace

// 平らな AABB 床への落下: span path と world path で接地と位置が一致する
TEST(CharacterControllerWorldTest, ParityOnFlatAabbFloor)
{
    const std::array<AABB, 1> boxes{MakeBox({0.0f, 0.0f, 0.0f}, {2.0f, 0.5f, 2.0f})};

    PhysicsWorld world;
    world.AddAabb(boxes[0]);

    CharacterControllerInput spanInput;
    spanInput.position = Vector3{0.0f, 1.4f, 0.0f};
    spanInput.velocity = Vector3{0.0f, -6.0f, 0.0f};
    spanInput.dt = 0.1f;
    spanInput.world = std::span<const AABB>(boxes);

    CharacterControllerInput worldInput = spanInput;
    worldInput.world = {};
    worldInput.physicsWorld = &world;

    CharacterController cc;
    const CharacterControllerResult rSpan = cc.Update(spanInput);
    const CharacterControllerResult rWorld = cc.Update(worldInput);

    EXPECT_TRUE(rWorld.grounded);
    ExpectResultsEqual(rSpan, rWorld);
}

// 45 度回転した薄い OBB 壁: 両 path で壁手前に停止し結果が一致する
TEST(CharacterControllerWorldTest, ParityOnRotatedObbWall)
{
    const Quaternion rot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, kPi / 4.0f);
    const std::array<OBB, 1> obbs{MakeObb({0.0f, 0.0f, 0.0f}, rot, {0.1f, 2.0f, 2.0f})};

    PhysicsWorld world;
    world.AddObb(obbs[0]);

    CharacterControllerInput spanInput;
    spanInput.position = Vector3{-3.0f, 0.0f, 0.0f};
    spanInput.velocity = Vector3{10.0f, 0.0f, 0.0f};
    spanInput.dt = 0.1f;
    spanInput.worldObbs = std::span<const OBB>(obbs);

    CharacterControllerInput worldInput = spanInput;
    worldInput.worldObbs = {};
    worldInput.physicsWorld = &world;

    CharacterController cc;
    const CharacterControllerResult rSpan = cc.Update(spanInput);
    const CharacterControllerResult rWorld = cc.Update(worldInput);

    EXPECT_LT(rWorld.position.x, -0.2f);
    ExpectResultsEqual(rSpan, rWorld);
}

// 球コライダーへの横移動: 両 path で結果が一致する
TEST(CharacterControllerWorldTest, ParityOnSphere)
{
    Sphere s;
    s.center = Vector3{0.0f, 0.0f, 0.0f};
    s.radius = 0.5f;
    const std::array<Sphere, 1> spheres{s};

    PhysicsWorld world;
    world.AddSphere(s);

    CharacterControllerInput spanInput;
    spanInput.position = Vector3{-3.0f, 0.0f, 0.0f};
    spanInput.velocity = Vector3{10.0f, 0.0f, 0.0f};
    spanInput.dt = 0.1f;
    spanInput.worldSpheres = std::span<const Sphere>(spheres);

    CharacterControllerInput worldInput = spanInput;
    worldInput.worldSpheres = {};
    worldInput.physicsWorld = &world;

    CharacterController cc;
    const CharacterControllerResult rSpan = cc.Update(spanInput);
    const CharacterControllerResult rWorld = cc.Update(worldInput);

    ExpectResultsEqual(rSpan, rWorld);
}
