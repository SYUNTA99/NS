#include <gtest/gtest.h>

#include <Framework/Core/Math.h>
#include <Framework/Physics/CharacterController.h>

#include <array>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Physics::CharacterController;
    using NS::Physics::CharacterControllerInput;
    using NS::Physics::CharacterControllerResult;

    AABB MakeAABB(const Vector3& center, const Vector3& extents)
    {
        AABB box;
        box.Center = center;
        box.Extents = extents;
        return box;
    }
} // namespace

TEST(CharacterControllerTest, FreeFallProgressesWhenWorldIsEmpty)
{
    CharacterController cc;
    const std::array<AABB, 0> world{};

    CharacterControllerInput input;
    input.position = {0.0f, 5.0f, 0.0f};
    input.velocity = {0.0f, -10.0f, 0.0f};
    input.dt = 0.1f;
    input.world = std::span<const AABB>{world};

    const CharacterControllerResult r = cc.Update(input);
    EXPECT_LT(r.position.y, 5.0f);
    EXPECT_FALSE(r.grounded);
}

TEST(CharacterControllerTest, LandsOnFloorAndReportsGrounded)
{
    CharacterController cc;
    const std::array<AABB, 1> world = {MakeAABB({0.0f, 0.0f, 0.0f}, {5.0f, 0.5f, 5.0f})};

    CharacterControllerInput input;
    // Capsule 底端 = position.y - halfHeight - radius = 2 - 0.5 - 0.4 = 1.1
    // 床上面 = 0.5。差分 0.6m を 1 step で 0.7m 落下させて確実に着地
    input.position = {0.0f, 2.0f, 0.0f};
    input.velocity = {0.0f, -7.0f, 0.0f};
    input.dt = 0.1f;
    input.world = std::span<const AABB>{world};

    const CharacterControllerResult r = cc.Update(input);
    EXPECT_TRUE(r.grounded);
    EXPECT_NEAR(r.position.y, 1.4f, 0.05f); // 床上面 0.5 + halfHeight 0.5 + radius 0.4 = 1.4
    EXPECT_GE(r.velocity.y, -0.01f);        // 着地で下方向 velocity は除去 or 0 付近
}

TEST(CharacterControllerTest, NoGroundedWhenAirborne)
{
    CharacterController cc;
    const std::array<AABB, 1> world = {MakeAABB({0.0f, 0.0f, 0.0f}, {5.0f, 0.5f, 5.0f})};

    CharacterControllerInput input;
    input.position = {0.0f, 10.0f, 0.0f}; // 床から 10m 上空
    input.velocity = {0.0f, 0.0f, 0.0f};
    input.dt = 0.016f;
    input.world = std::span<const AABB>{world};

    const CharacterControllerResult r = cc.Update(input);
    EXPECT_FALSE(r.grounded);
}

TEST(CharacterControllerTest, StopsAtWallAndSlidesAlongIt)
{
    CharacterController cc;
    const std::array<AABB, 1> world = {MakeAABB({5.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 5.0f})};

    CharacterControllerInput input;
    input.position = {0.0f, 1.0f, 0.0f};
    input.velocity = {20.0f, 0.0f, 0.0f}; // 壁に向かって突進
    input.dt = 0.1f;
    input.world = std::span<const AABB>{world};

    const CharacterControllerResult r = cc.Update(input);
    // 壁面 (西側 x=4.5) と Capsule radius 0.4 = position.x の上限 = 4.1
    EXPECT_LE(r.position.x, 4.2f);
}

TEST(CharacterControllerTest, DeterministicUnderSameInput)
{
    CharacterController cc;
    const std::array<AABB, 1> world = {MakeAABB({0.0f, 0.0f, 0.0f}, {5.0f, 0.5f, 5.0f})};

    CharacterControllerInput input;
    input.position = {0.0f, 3.0f, 0.0f};
    input.velocity = {1.0f, -2.0f, 0.5f};
    input.dt = 0.0167f;
    input.world = std::span<const AABB>{world};

    const auto a = cc.Update(input);
    const auto b = cc.Update(input);
    EXPECT_FLOAT_EQ(a.position.x, b.position.x);
    EXPECT_FLOAT_EQ(a.position.y, b.position.y);
    EXPECT_FLOAT_EQ(a.position.z, b.position.z);
    EXPECT_EQ(a.grounded, b.grounded);
}
