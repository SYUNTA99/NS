#include <Runtime/Core/Math.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/BoxColliderComponent.h>
#include <Runtime/Object/Components/CapsuleColliderComponent.h>
#include <Runtime/Object/Components/MeshColliderComponent.h>
#include <Runtime/Object/Components/SlopeColliderComponent.h>
#include <Runtime/Object/Components/SphereColliderComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/MeshCollision.h>
#include <Runtime/Physics/PhysicsWorld.h>

#include <filesystem>
#include <gtest/gtest.h>
#include <vector>

namespace
{
    using NS::Core::Vector3;
    using NS::Object::BoxColliderComponent;
    using NS::Object::CapsuleColliderComponent;
    using NS::Object::GameObject;
    using NS::Object::MeshColliderComponent;
    using NS::Object::SlopeColliderComponent;
    using NS::Object::SphereColliderComponent;
    using NS::Physics::PhysicsWorld;
    using NS::Physics::Triangle;

    std::vector<Triangle> MakeFloorQuad()
    {
        return {
            Triangle{Vector3{-1.0f, 0.0f, -1.0f}, Vector3{1.0f, 0.0f, 1.0f}, Vector3{1.0f, 0.0f, -1.0f}},
            Triangle{Vector3{-1.0f, 0.0f, -1.0f}, Vector3{-1.0f, 0.0f, 1.0f}, Vector3{1.0f, 0.0f, 1.0f}},
        };
    }
} // namespace

TEST(ColliderJolt, BoxCreatesOneBody)
{
    GameObject owner;
    auto* box = owner.AddComponent<BoxColliderComponent>();
    owner.Root().SetPosition(Vector3{2.0f, 3.0f, 4.0f});
    PhysicsWorld world;

    box->SyncToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_FALSE(box->BodyId().IsInvalid());
    EXPECT_NEAR(world.BodyPosition(box->BodyId()).y, 3.0f, 1.0e-4f);
}

TEST(ColliderJolt, TriggerBoxCreatesASensorBody)
{
    GameObject owner;
    auto* box = owner.AddComponent<BoxColliderComponent>();
    box->SetTrigger(true);
    PhysicsWorld world;

    box->SyncToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_FALSE(box->BodyId().IsInvalid());
}

TEST(ColliderJolt, SphereCreatesOneBody)
{
    GameObject owner;
    auto* sphere = owner.AddComponent<SphereColliderComponent>();
    sphere->SetRadius(0.75f);
    PhysicsWorld world;

    sphere->SyncToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_FALSE(sphere->BodyId().IsInvalid());
}

TEST(ColliderJolt, CapsuleCreatesOneBody)
{
    GameObject owner;
    auto* capsule = owner.AddComponent<CapsuleColliderComponent>();
    PhysicsWorld world;

    capsule->SyncToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_FALSE(capsule->BodyId().IsInvalid());
}

TEST(ColliderJolt, ExcludedCapsuleCreatesNoBody)
{
    GameObject owner;
    auto* capsule = owner.AddComponent<CapsuleColliderComponent>();
    capsule->SetExcludedFromStaticWorld(true);
    PhysicsWorld world;

    capsule->SyncToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 0u);
    EXPECT_TRUE(capsule->BodyId().IsInvalid());
}

TEST(ColliderJolt, MeshCreatesOneBodyForAllTriangles)
{
    NS::Physics::MeshCollision floor{MakeFloorQuad(), nullptr};
    floor.shape = NS::Physics::CreateMeshShape(floor.triangles);
    GameObject owner;
    auto* mesh = owner.AddComponent<MeshColliderComponent>();
    mesh->SetCollision(&floor);
    PhysicsWorld world;

    mesh->SyncToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_FALSE(mesh->BodyId().IsInvalid());
}

// 同じ資産を置いた 2 体は形を作り直さず共有する。 body が 1 つずつ参照を持つので参照数が 2 増える
// 2 体目は x = 5 に横 2 倍で置いたので、 横に広がった x = 5.9 でも上面 (y = 0.5) に当たる
TEST(ColliderJolt, PlacedMeshCollidersShareOneShape)
{
    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    const NS::Physics::MeshCollision* cube = assets.GetOrLoadMeshCollision("cube");
    ASSERT_NE(cube, nullptr);
    ASSERT_NE(cube->shape, nullptr);
    const JPH::uint32 before = cube->shape->GetRefCount();

    GameObject first;
    auto* firstMesh = first.AddComponent<MeshColliderComponent>();
    firstMesh->SetCollision(cube);
    GameObject second;
    second.Root().SetPosition(Vector3{5.0f, 0.0f, 0.0f});
    second.Root().SetScale(Vector3{2.0f, 1.0f, 1.0f});
    auto* secondMesh = second.AddComponent<MeshColliderComponent>();
    secondMesh->SetCollision(cube);
    PhysicsWorld world;

    firstMesh->SyncToPhysics(world);
    secondMesh->SyncToPhysics(world);

    EXPECT_EQ(cube->shape->GetRefCount(), before + 2);
    float distance = 0.0f;
    ASSERT_TRUE(world.RaycastDown(Vector3{5.9f, 2.0f, 0.0f}, 8.0f, distance));
    EXPECT_NEAR(distance, 1.5f, 1.0e-3f);
}

// 横 2 倍の親の下で 45 度回した cube は、 描画では x 方向に ±1.41 まで伸びた菱形になる
// 歪んだ行列は分解できず、 DecomposeAffine は既定値 (拡縮 1・回転無し・原点) のまま返る
// 共有の形で置くと原点の 1 x 1 の箱になり、 x = 0.5 までしか届かない
// x = 1.2 に当たるのは描画と同じ形の時だけ
TEST(ColliderJolt, ShearedMeshColliderKeepsTheDrawnShape)
{
    NS::Object::AssetManager assets{std::filesystem::path{"."}};
    const NS::Physics::MeshCollision* cube = assets.GetOrLoadMeshCollision("cube");
    ASSERT_NE(cube, nullptr);

    GameObject parent;
    parent.Root().SetScale(Vector3{2.0f, 1.0f, 1.0f});
    GameObject child;
    child.SetParent(&parent);
    child.Root().SetRotation(
        NS::Core::Quaternion::CreateFromAxisAngle(Vector3::UnitY, NS::Core::ToRadians(NS::Core::Degrees{45.0f}).value));
    auto* mesh = child.AddComponent<MeshColliderComponent>();
    mesh->SetCollision(cube);
    PhysicsWorld world;

    mesh->SyncToPhysics(world);

    float distance = 0.0f;
    ASSERT_TRUE(world.RaycastDown(Vector3{1.2f, 2.0f, 0.0f}, 8.0f, distance));
    EXPECT_NEAR(distance, 1.5f, 1.0e-3f);
}

TEST(ColliderJolt, EmptyMeshCreatesNoBody)
{
    GameObject owner;
    auto* mesh = owner.AddComponent<MeshColliderComponent>();
    PhysicsWorld world;

    mesh->SyncToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 0u);
    EXPECT_TRUE(mesh->BodyId().IsInvalid());
}

TEST(ColliderJolt, SlopeCreatesOneBody)
{
    GameObject owner;
    auto* slope = owner.AddComponent<SlopeColliderComponent>();
    PhysicsWorld world;

    slope->SyncToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_FALSE(slope->BodyId().IsInvalid());
}

TEST(ColliderJolt, SyncingTwiceKeepsTheBodyId)
{
    GameObject owner;
    auto* box = owner.AddComponent<BoxColliderComponent>();
    PhysicsWorld world;

    box->SyncToPhysics(world);
    const JPH::BodyID first = box->BodyId();
    box->SyncToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_EQ(box->BodyId(), first);
}

TEST(ColliderJolt, SyncingMovedBoxKeepsTheBodyIdAndMovesTheJoltBody)
{
    GameObject owner;
    auto* box = owner.AddComponent<BoxColliderComponent>();
    PhysicsWorld world;

    box->SyncToPhysics(world);
    const JPH::BodyID first = box->BodyId();
    owner.Root().SetPosition(Vector3{4.0f, 5.0f, 6.0f});
    box->SyncToPhysics(world);

    EXPECT_EQ(box->BodyId(), first);
    EXPECT_NEAR(world.BodyPosition(first).x, 4.0f, 1.0e-4f);
    EXPECT_NEAR(world.BodyPosition(first).y, 5.0f, 1.0e-4f);
    EXPECT_NEAR(world.BodyPosition(first).z, 6.0f, 1.0e-4f);
}

TEST(ColliderJolt, SyncingResizedBoxKeepsTheBodyIdAndUpdatesTheJoltShape)
{
    GameObject owner;
    auto* box = owner.AddComponent<BoxColliderComponent>();
    PhysicsWorld world;

    box->SyncToPhysics(world);
    const JPH::BodyID first = box->BodyId();
    float distance = 0.0f;
    EXPECT_FALSE(world.RaycastDown(Vector3{1.5f, 2.0f, 0.0f}, 4.0f, distance));

    box->SetHalfExtents(Vector3{2.0f, 0.5f, 0.5f});
    box->SyncToPhysics(world);

    EXPECT_EQ(box->BodyId(), first);
    EXPECT_TRUE(world.RaycastDown(Vector3{1.5f, 2.0f, 0.0f}, 4.0f, distance));
}

TEST(ColliderJolt, EndPlayRemovesItsOwnBody)
{
    GameObject owner;
    auto* box = owner.AddComponent<BoxColliderComponent>();
    PhysicsWorld world;

    box->SyncToPhysics(world);
    box->OnEndPlay();

    EXPECT_EQ(world.BodyCount(), 0u);
    EXPECT_TRUE(box->BodyId().IsInvalid());
}

TEST(ColliderJolt, EndPlayWithoutABodyLeavesTheWorldAlone)
{
    GameObject owner;
    auto* box = owner.AddComponent<BoxColliderComponent>();
    PhysicsWorld world;

    box->OnEndPlay();

    EXPECT_EQ(world.BodyCount(), 0u);
    EXPECT_TRUE(box->BodyId().IsInvalid());
}
