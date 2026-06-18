#include <gtest/gtest.h>

#include <Framework/Math/Math.h>
#include <Framework/Physics/CharacterController.h>
#include <Framework/Physics/SweptOBB.h>

#include <array>

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
    using NS::Physics::Triangle;

    constexpr float kPi = 3.14159265358979323846f;
} // namespace

// 平らな OBB 床へ落下すると接地する
TEST(ObbCollisionTest, CapsuleLandsOnFlatObb)
{
    CharacterController cc;
    const std::array<AABB, 0> emptyWorld{};
    const std::array<Triangle, 0> emptyTris{};
    const std::array<OBB, 1> obbs{MakeObb({0.0f, 0.0f, 0.0f}, Quaternion::Identity, {2.0f, 0.5f, 2.0f})};

    CharacterControllerInput input;
    input.position = Vector3{0.0f, 1.5f, 0.0f};
    input.velocity = Vector3{0.0f, -8.0f, 0.0f};
    input.dt = 0.1f;
    input.world = std::span<const AABB>{emptyWorld};
    input.worldTriangles = std::span<const Triangle>{emptyTris};
    input.worldObbs = std::span<const OBB>{obbs};

    const CharacterControllerResult r = cc.Update(input);
    EXPECT_TRUE(r.grounded);
    EXPECT_GT(r.contactNormal.y, 0.7f);
}

// 平らな OBB 床に静止していると接地判定が維持される (grounded probe の OBB パス)
TEST(ObbCollisionTest, CapsuleRestingOnObbStaysGrounded)
{
    CharacterController cc;
    const std::array<AABB, 0> emptyWorld{};
    const std::array<Triangle, 0> emptyTris{};
    const std::array<OBB, 1> obbs{MakeObb({0.0f, 0.0f, 0.0f}, Quaternion::Identity, {2.0f, 0.5f, 2.0f})};

    // capsule 底端 (center.y - halfHeight = 0.9) が床上面 0.5 から radius 内に収まる静止姿勢
    CharacterControllerInput input;
    input.position = Vector3{0.0f, 1.4f, 0.0f};
    input.velocity = Vector3{0.0f, 0.0f, 0.0f};
    input.dt = 1.0f / 60.0f;
    input.world = std::span<const AABB>{emptyWorld};
    input.worldTriangles = std::span<const Triangle>{emptyTris};
    input.worldObbs = std::span<const OBB>{obbs};

    const CharacterControllerResult r = cc.Update(input);
    EXPECT_TRUE(r.grounded);
}

// Y 回転した薄い壁を貫通せず手前で止まる
TEST(ObbCollisionTest, CapsuleStopsAtRotatedWall)
{
    CharacterController cc;
    const std::array<AABB, 0> emptyWorld{};
    const std::array<Triangle, 0> emptyTris{};
    const Quaternion rot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, kPi / 4.0f);
    const std::array<OBB, 1> obbs{MakeObb({0.0f, 0.0f, 0.0f}, rot, {0.1f, 2.0f, 2.0f})};

    Vector3 position{-3.0f, 0.0f, 0.0f};
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i)
    {
        CharacterControllerInput in;
        in.position = position;
        in.velocity = Vector3{10.0f, 0.0f, 0.0f};
        in.dt = dt;
        in.world = std::span<const AABB>{emptyWorld};
        in.worldTriangles = std::span<const Triangle>{emptyTris};
        in.worldObbs = std::span<const OBB>{obbs};
        const CharacterControllerResult r = cc.Update(in);
        position = r.position;
    }

    // 衝突無しなら 10×(1/60)×30frame で x は +2 付近まで抜ける。 OBB が効けば貫通せず
    // 接触法線 (-0.707,0,+0.707) で +Z へ滑る。 z への偏向は回転を畳んだ AABB では起きない
    EXPECT_LT(position.x, 1.5f);
    EXPECT_GT(position.z, 0.3f);
}
