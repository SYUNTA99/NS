#include <Game/Player/PlayerComponent.h>
#include <Runtime/Core/Sphere.h>
#include <Runtime/Object/Components/CapsuleCollider.h>
#include <Runtime/Object/Components/MeshCollider.h>
#include <Runtime/Object/Components/SphereCollider.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/MeshCollision.h>
#include <Runtime/Physics/PhysicsScene.h>
#include <gtest/gtest.h>
#include <vector>

namespace
{
    using NS::Core::Vector3;
} // namespace

TEST(SphereColliderTest, DefaultRadiusIsHalfMeter)
{
    NS::Obj::SphereCollider sc;
    EXPECT_FLOAT_EQ(sc.Radius(), 0.5f);

    const NS::Core::Sphere s = sc.WorldSphere();
    EXPECT_FLOAT_EQ(s.center.x, 0.0f);
    EXPECT_FLOAT_EQ(s.radius, 0.5f);
}

TEST(SphereColliderTest, WorldSphereReflectsOwnerPositionAndScale)
{
    NS::Obj::GameObject obj;
    obj.Root().SetPosition(Vector3{1.0f, 2.0f, 3.0f});
    obj.Root().SetScale(Vector3{2.0f, 2.0f, 2.0f});
    NS::Obj::SphereCollider& sc = *obj.AddComponent<NS::Obj::SphereCollider>(0.5f);

    const NS::Core::Sphere s = sc.WorldSphere();
    EXPECT_FLOAT_EQ(s.center.x, 1.0f);
    EXPECT_FLOAT_EQ(s.center.y, 2.0f);
    EXPECT_FLOAT_EQ(s.center.z, 3.0f);
    EXPECT_FLOAT_EQ(s.radius, 1.0f); // 0.5 * scale 2
}

TEST(SphereColliderTest, CenterOffsetShiftsAndScalesWithOwner)
{
    NS::Obj::GameObject obj;
    obj.Root().SetScale(Vector3{2.0f, 2.0f, 2.0f});
    NS::Obj::SphereCollider& sc = *obj.AddComponent<NS::Obj::SphereCollider>(0.5f);
    sc.SetCenterOffset(Vector3{1.0f, 0.0f, 0.0f});

    const NS::Core::Sphere s = sc.WorldSphere();
    EXPECT_FLOAT_EQ(s.center.x, 2.0f); // offset 1 * scale 2
}

TEST(SphereColliderTest, NegativeRadiusClampsToZero)
{
    NS::Obj::SphereCollider sc;
    sc.SetRadius(-3.0f);
    EXPECT_FLOAT_EQ(sc.Radius(), 0.0f);
}

TEST(CapsuleColliderTest, DefaultsAreVertical)
{
    NS::Obj::CapsuleCollider cc;
    EXPECT_FLOAT_EQ(cc.Radius(), 0.4f);
    EXPECT_FLOAT_EQ(cc.HalfHeight(), 0.5f);

    const NS::Phys::Capsule capsule = cc.WorldCapsule();
    EXPECT_NEAR(capsule.axis.x, 0.0f, 1e-5f);
    EXPECT_NEAR(capsule.axis.y, 1.0f, 1e-5f);
    EXPECT_NEAR(capsule.axis.z, 0.0f, 1e-5f);
    EXPECT_FLOAT_EQ(capsule.radius, 0.4f);
    EXPECT_FLOAT_EQ(capsule.halfHeight, 0.5f);
}

TEST(CapsuleColliderTest, WorldPropertiesReflectOwnerScale)
{
    NS::Obj::GameObject obj;
    obj.Root().SetPosition(Vector3{0.0f, 5.0f, 0.0f});
    obj.Root().SetScale(Vector3{2.0f, 3.0f, 2.0f});
    NS::Obj::CapsuleCollider& cc = *obj.AddComponent<NS::Obj::CapsuleCollider>(0.4f, 0.5f);

    const NS::Phys::Capsule capsule = cc.WorldCapsule();
    EXPECT_NEAR(capsule.center.y, 5.0f, 1e-5f);
    EXPECT_NEAR(capsule.radius, 0.8f, 1e-5f);     // 0.4 * max(scale.x, scale.z)=2
    EXPECT_NEAR(capsule.halfHeight, 1.5f, 1e-5f); // 0.5 * scale.y=3
    EXPECT_NEAR(capsule.axis.y, 1.0f, 1e-5f);
}

// 自分で掃引して動く配置物の capsule を静的世界へ入れると、掃引が自分に当たって動けなくなる
// 判定は型でなく SetExcludedFromStaticWorld の値で見る。EntityComponent の OnStart が同居の capsule へ設定する
TEST(CapsuleColliderTest, SyncToPhysicsSkipsTheOwnerThatSweepsItself)
{
    NS::Obj::GameObject obj;
    NS::Obj::CapsuleCollider& cc = *obj.AddComponent<NS::Obj::CapsuleCollider>(0.4f, 0.5f);
    obj.AddComponent<NS::Game::Player::PlayerComponent>();
    obj.OnStart();

    NS::Phys::PhysicsScene physics;
    cc.SyncToPhysics(physics);
    EXPECT_EQ(physics.BodyCount(), 0u);
}

TEST(CapsuleColliderTest, RotationEulerDegreesRoundTrips)
{
    NS::Obj::CapsuleCollider cc;
    cc.SetRotationEulerDegrees(Vector3{0.0f, 90.0f, 0.0f});
    EXPECT_NEAR(cc.RotationEulerDegrees().y, 90.0f, 1e-3f);
}

TEST(MeshColliderTest, WorldTrianglesTransformByOwnerPosition)
{
    const NS::Phys::MeshCollision collision{
        {NS::Phys::Triangle{Vector3{0.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}}},
        nullptr};

    NS::Obj::GameObject obj;
    obj.Root().SetPosition(Vector3{10.0f, 0.0f, 0.0f});
    NS::Obj::MeshCollider& cc = *obj.AddComponent<NS::Obj::MeshCollider>();
    cc.SetCollision(&collision);

    const std::vector<NS::Phys::Triangle> world = cc.WorldTriangles();
    ASSERT_EQ(world.size(), 1u);
    EXPECT_NEAR(world[0].v0.x, 10.0f, 1e-5f);
    EXPECT_NEAR(world[0].v1.x, 11.0f, 1e-5f);
    EXPECT_NEAR(world[0].v2.x, 10.0f, 1e-5f);
    EXPECT_NEAR(world[0].v2.y, 1.0f, 1e-5f);
}

TEST(MeshColliderTest, WithoutOwnerReturnsLocalUnchanged)
{
    const NS::Phys::MeshCollision collision{
        {NS::Phys::Triangle{Vector3{0.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}}},
        nullptr};
    NS::Obj::MeshCollider cc;
    cc.SetCollision(&collision);

    const std::vector<NS::Phys::Triangle> world = cc.WorldTriangles();
    ASSERT_EQ(world.size(), 1u);
    EXPECT_FLOAT_EQ(world[0].v1.x, 1.0f);
}
