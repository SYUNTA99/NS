#include <gtest/gtest.h>

#include <Framework/Math/Math.h>
#include <Framework/Physics/CharacterController.h>
#include <Framework/Physics/PhysicsWorld.h>

#include <array>
#include <span>

namespace
{
    using NS::Math::AABB;
    using NS::Math::Vector3;
    using NS::Physics::CharacterController;
    using NS::Physics::CharacterControllerInput;
    using NS::Physics::CharacterControllerResult;
    using NS::Physics::PhysicsWorld;

    AABB MakeAABB(const Vector3& center, const Vector3& extents)
    {
        AABB box;
        box.Center = center;
        box.Extents = extents;
        return box;
    }

    // AABB 群を積んで broadphase まで作った physics world を返す
    PhysicsWorld MakeWorld(std::span<const AABB> boxes)
    {
        PhysicsWorld world;
        for (const AABB& b : boxes)
            world.AddAabb(b);
        world.BuildBroadphase();
        return world;
    }
} // namespace

TEST(CharacterControllerTest, FreeFallProgressesWhenWorldIsEmpty)
{
    CharacterController cc;

    // world 未設定 (physicsWorld == nullptr) は衝突なし = 自由落下
    CharacterControllerInput input;
    input.position = Vector3{0.0f, 5.0f, 0.0f};
    input.velocity = Vector3{0.0f, -10.0f, 0.0f};
    input.dt = 0.1f;

    const CharacterControllerResult r = cc.Update(input);
    EXPECT_LT(r.position.y, 5.0f);
    EXPECT_FALSE(r.grounded);
}

TEST(CharacterControllerTest, LandsOnFloorAndReportsGrounded)
{
    CharacterController cc;
    const std::array<AABB, 1> world = {MakeAABB({0.0f, 0.0f, 0.0f}, {5.0f, 0.5f, 5.0f})};
    PhysicsWorld pw = MakeWorld(world);

    CharacterControllerInput input;
    // Capsule 底端 = position.y - halfHeight - radius = 2 - 0.5 - 0.4 = 1.1
    // 床上面 = 0.5。差分 0.6m を 1 step で 0.7m 落下させて確実に着地
    input.position = Vector3{0.0f, 2.0f, 0.0f};
    input.velocity = Vector3{0.0f, -7.0f, 0.0f};
    input.dt = 0.1f;
    input.physicsWorld = &pw;

    const CharacterControllerResult r = cc.Update(input);
    EXPECT_TRUE(r.grounded);
    EXPECT_NEAR(r.position.y, 1.4f, 0.05f); // 床上面 0.5 + halfHeight 0.5 + radius 0.4 = 1.4
    EXPECT_GE(r.velocity.y, -0.01f);        // 着地で下方向 velocity は除去 or 0 付近
}

TEST(CharacterControllerTest, NoGroundedWhenAirborne)
{
    CharacterController cc;
    const std::array<AABB, 1> world = {MakeAABB({0.0f, 0.0f, 0.0f}, {5.0f, 0.5f, 5.0f})};
    PhysicsWorld pw = MakeWorld(world);

    CharacterControllerInput input;
    input.position = Vector3{0.0f, 10.0f, 0.0f}; // 床から 10m 上空
    input.velocity = Vector3{0.0f, 0.0f, 0.0f};
    input.dt = 0.016f;
    input.physicsWorld = &pw;

    const CharacterControllerResult r = cc.Update(input);
    EXPECT_FALSE(r.grounded);
}

TEST(CharacterControllerTest, StopsAtWallAndSlidesAlongIt)
{
    CharacterController cc;
    const std::array<AABB, 1> world = {MakeAABB({5.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 5.0f})};
    PhysicsWorld pw = MakeWorld(world);

    CharacterControllerInput input;
    input.position = Vector3{0.0f, 1.0f, 0.0f};
    input.velocity = Vector3{20.0f, 0.0f, 0.0f}; // 壁に向かって突進
    input.dt = 0.1f;
    input.physicsWorld = &pw;

    const CharacterControllerResult r = cc.Update(input);
    // 壁面 (西側 x=4.5) と Capsule radius 0.4 = position.x の上限 = 4.1
    EXPECT_LE(r.position.x, 4.2f);
}

TEST(CharacterControllerTest, DeterministicUnderSameInput)
{
    CharacterController cc;
    const std::array<AABB, 1> world = {MakeAABB({0.0f, 0.0f, 0.0f}, {5.0f, 0.5f, 5.0f})};
    PhysicsWorld pw = MakeWorld(world);

    CharacterControllerInput input;
    input.position = Vector3{0.0f, 3.0f, 0.0f};
    input.velocity = Vector3{1.0f, -2.0f, 0.5f};
    input.dt = 0.0167f;
    input.physicsWorld = &pw;

    const auto a = cc.Update(input);
    const auto b = cc.Update(input);
    EXPECT_FLOAT_EQ(a.position.x, b.position.x);
    EXPECT_FLOAT_EQ(a.position.y, b.position.y);
    EXPECT_FLOAT_EQ(a.position.z, b.position.z);
    EXPECT_EQ(a.grounded, b.grounded);
}
