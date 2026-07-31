#include <gtest/gtest.h>
#include <Runtime/Math/Math.h>
#include <Runtime/Physics/CapsuleMover.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <Runtime/Physics/SweptOBB.h>

namespace
{
    using NS::Math::Quaternion;
    using NS::Math::Vector3;
    using NS::Physics::CapsuleMover;
    using NS::Physics::CapsuleMoverInput;
    using NS::Physics::CapsuleMoverResult;
    using NS::Physics::MakeObb;
    using NS::Physics::PhysicsWorld;

    constexpr float k_Pi = 3.14159265358979323846f;
} // namespace

// 平らな OBB 床へ落下すると接地する
TEST(ObbCollisionTest, CapsuleLandsOnFlatObb)
{
    CapsuleMover cc;
    PhysicsWorld world;
    world.AddOBB(MakeObb({0.0f, 0.0f, 0.0f}, Quaternion::Identity, {2.0f, 0.5f, 2.0f}));
    world.BuildBroadphase();

    CapsuleMoverInput input;
    input.position = Vector3{0.0f, 1.5f, 0.0f};
    input.velocity = Vector3{0.0f, -8.0f, 0.0f};
    input.dt = 0.1f;
    input.physicsWorld = &world;

    const CapsuleMoverResult r = cc.Update(input);
    EXPECT_TRUE(r.grounded);
    EXPECT_GT(r.contactNormal.y, 0.7f);
}

// 平らな OBB 床に静止していても接地判定が続く。grounded probe の OBB 経路を通す
TEST(ObbCollisionTest, CapsuleRestingOnObbStaysGrounded)
{
    CapsuleMover cc;
    PhysicsWorld world;
    world.AddOBB(MakeObb({0.0f, 0.0f, 0.0f}, Quaternion::Identity, {2.0f, 0.5f, 2.0f}));
    world.BuildBroadphase();

    // capsule 底端 (center.y - halfHeight = 0.9) が床上面 0.5 から radius 内に収まる静止姿勢
    CapsuleMoverInput input;
    input.position = Vector3{0.0f, 1.4f, 0.0f};
    input.velocity = Vector3{0.0f, 0.0f, 0.0f};
    input.dt = 1.0f / 60.0f;
    input.physicsWorld = &world;

    const CapsuleMoverResult r = cc.Update(input);
    EXPECT_TRUE(r.grounded);
}

// Y 回転した薄い壁を貫通せず手前で止まる
TEST(ObbCollisionTest, CapsuleStopsAtRotatedWall)
{
    CapsuleMover cc;
    const Quaternion rot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, k_Pi / 4.0f);
    PhysicsWorld world;
    world.AddOBB(MakeObb({0.0f, 0.0f, 0.0f}, rot, {0.1f, 2.0f, 2.0f}));
    world.BuildBroadphase();

    Vector3 position{-3.0f, 0.0f, 0.0f};
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i)
    {
        CapsuleMoverInput in;
        in.position = position;
        in.velocity = Vector3{10.0f, 0.0f, 0.0f};
        in.dt = dt;
        in.physicsWorld = &world;
        const CapsuleMoverResult r = cc.Update(in);
        position = r.position;
    }

    // 衝突無しなら 10×(1/60)×30frame で x は +2 付近まで抜ける。 OBB が効けば貫通せず
    // 接触法線 (-0.707,0,+0.707) で +Z へ滑る。 z への偏向は回転を畳んだ AABB では起きない
    EXPECT_LT(position.x, 1.5f);
    EXPECT_GT(position.z, 0.3f);
}
