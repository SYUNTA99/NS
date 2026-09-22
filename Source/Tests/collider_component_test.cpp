#include <Runtime/Core/Math.h>
#include <Runtime/Object/AssetManager.h>
#include <Runtime/Object/Components/BoxColliderComponent.h>
#include <Runtime/Object/Components/CapsuleColliderComponent.h>
#include <Runtime/Object/Components/MeshColliderComponent.h>
#include <Runtime/Object/Components/SlopeColliderComponent.h>
#include <Runtime/Object/Components/SphereColliderComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/MeshCollision.h>
#include <Runtime/Physics/PhysicsScene.h>

#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace
{
    using NS::Core::Vector3;
    using NS::Obj::BoxColliderComponent;
    using NS::Obj::CapsuleColliderComponent;
    using NS::Obj::GameObject;
    using NS::Obj::MeshColliderComponent;
    using NS::Obj::SlopeColliderComponent;
    using NS::Obj::SphereColliderComponent;
    using NS::Phys::PhysicsScene;
    using NS::Phys::Triangle;

    std::vector<Triangle> MakeFloorQuad()
    {
        return {
            Triangle{Vector3{-1.0f, 0.0f, -1.0f}, Vector3{1.0f, 0.0f, 1.0f}, Vector3{1.0f, 0.0f, -1.0f}},
            Triangle{Vector3{-1.0f, 0.0f, -1.0f}, Vector3{-1.0f, 0.0f, 1.0f}, Vector3{1.0f, 0.0f, 1.0f}},
        };
    }

    // collider は持ち主の Scene の PhysicsScene しか受け取らない。持ち主を Scene の中へ湧かして揃える
    struct ColliderStage
    {
        NS::Obj::Scene scene;
        GameObject& owner = *scene.SpawnTransient<GameObject>();
        PhysicsScene& physics = scene.Physics();
    };
} // namespace

TEST(ColliderJolt, BoxCreatesOneBody)
{
    ColliderStage stage;
    auto* box = stage.owner.AddComponent<BoxColliderComponent>();
    stage.owner.Root().SetPosition(Vector3{2.0f, 3.0f, 4.0f});

    box->SyncToPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 1u);
    EXPECT_FALSE(box->BodyId().IsInvalid());
    EXPECT_NEAR(stage.physics.BodyPosition(box->BodyId()).y, 3.0f, 1.0e-4f);
}

TEST(ColliderJolt, TriggerBoxCreatesASensorBody)
{
    ColliderStage stage;
    auto* box = stage.owner.AddComponent<BoxColliderComponent>();
    box->SetTrigger(true);

    box->SyncToPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 1u);
    EXPECT_FALSE(box->BodyId().IsInvalid());
}

TEST(ColliderJolt, SphereCreatesOneBody)
{
    ColliderStage stage;
    auto* sphere = stage.owner.AddComponent<SphereColliderComponent>();
    sphere->SetRadius(0.75f);

    sphere->SyncToPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 1u);
    EXPECT_FALSE(sphere->BodyId().IsInvalid());
}

TEST(ColliderJolt, CapsuleCreatesOneBody)
{
    ColliderStage stage;
    auto* capsule = stage.owner.AddComponent<CapsuleColliderComponent>();

    capsule->SyncToPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 1u);
    EXPECT_FALSE(capsule->BodyId().IsInvalid());
}

// 固形の箱を隣に置くのは、body 0 個を期待すると全部断られても緑になるため
TEST(ColliderJolt, ExcludedCapsuleCreatesNoBody)
{
    ColliderStage stage;
    auto* solid = stage.owner.AddComponent<BoxColliderComponent>();
    auto* capsule = stage.owner.AddComponent<CapsuleColliderComponent>();
    capsule->SetExcludedFromStaticWorld(true);

    solid->SyncToPhysics(stage.physics);
    capsule->SyncToPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 1u);
    EXPECT_TRUE(capsule->BodyId().IsInvalid());
}

TEST(ColliderJolt, MeshCreatesOneBodyForAllTriangles)
{
    NS::Phys::MeshCollision floor{MakeFloorQuad(), nullptr};
    floor.shape = NS::Phys::CreateMeshShape(floor.triangles);
    ColliderStage stage;
    auto* mesh = stage.owner.AddComponent<MeshColliderComponent>();
    mesh->SetCollision(&floor);

    mesh->SyncToPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 1u);
    EXPECT_FALSE(mesh->BodyId().IsInvalid());
}

// 同じ資産を置いた 2 体は形を作り直さず共有する。置いた 1 体につき参照が 1 つ増える
// 2 体目は x = 5 に横 2 倍で置いたので、横に広がった x = 5.9 でも上面 (y = 0.5) に当たる
TEST(ColliderJolt, PlacedMeshCollidersShareOneShape)
{
    NS::Obj::AssetManager assets{std::string{"."}};
    const NS::Phys::MeshCollision* cube = assets.GetOrLoadMeshCollision("cube");
    ASSERT_NE(cube, nullptr);
    ASSERT_NE(cube->shape, nullptr);
    const JPH::uint32 before = cube->shape->GetRefCount();

    ColliderStage stage;
    auto* firstMesh = stage.owner.AddComponent<MeshColliderComponent>();
    firstMesh->SetCollision(cube);
    GameObject& second = *stage.scene.SpawnTransient<GameObject>();
    second.Root().SetPosition(Vector3{5.0f, 0.0f, 0.0f});
    second.Root().SetScale(Vector3{2.0f, 1.0f, 1.0f});
    auto* secondMesh = second.AddComponent<MeshColliderComponent>();
    secondMesh->SetCollision(cube);

    firstMesh->SyncToPhysics(stage.physics);
    secondMesh->SyncToPhysics(stage.physics);

    EXPECT_EQ(cube->shape->GetRefCount(), before + 2);
    float distance = 0.0f;
    ASSERT_TRUE(stage.physics.Raycast(Vector3{5.9f, 2.0f, 0.0f}, Vector3{0.0f, -1.0f, 0.0f}, 8.0f, distance));
    EXPECT_NEAR(distance, 1.5f, 1.0e-3f);
}

// 横 2 倍の親の下で 45 度回した cube は、描画では x 方向に ±1.41 まで伸びた菱形になる
// 歪んだ行列は分解できず、DecomposeAffine は既定値 (拡縮 1・回転無し・原点) のまま返る
// 共有の形で置くと原点の 1 x 1 の箱になり、x = 0.5 までしか届かない
// x = 1.2 に当たるのは描画と同じ形の時だけ
TEST(ColliderJolt, ShearedMeshColliderKeepsTheDrawnShape)
{
    NS::Obj::AssetManager assets{std::string{"."}};
    const NS::Phys::MeshCollision* cube = assets.GetOrLoadMeshCollision("cube");
    ASSERT_NE(cube, nullptr);

    ColliderStage stage;
    GameObject& parent = stage.owner;
    parent.Root().SetScale(Vector3{2.0f, 1.0f, 1.0f});
    GameObject& child = *stage.scene.SpawnTransient<GameObject>();
    child.SetParent(&parent);
    child.Root().SetRotation(
        NS::Core::Quaternion::CreateFromAxisAngle(Vector3::UnitY, NS::Core::ToRadians(NS::Core::Degrees{45.0f}).value));
    auto* mesh = child.AddComponent<MeshColliderComponent>();
    mesh->SetCollision(cube);

    mesh->SyncToPhysics(stage.physics);

    float distance = 0.0f;
    ASSERT_TRUE(stage.physics.Raycast(Vector3{1.2f, 2.0f, 0.0f}, Vector3{0.0f, -1.0f, 0.0f}, 8.0f, distance));
    EXPECT_NEAR(distance, 1.5f, 1.0e-3f);
}

// 隣の固形は ExcludedCapsuleCreatesNoBody と同じ役目
TEST(ColliderJolt, EmptyMeshCreatesNoBody)
{
    ColliderStage stage;
    auto* solid = stage.owner.AddComponent<BoxColliderComponent>();
    auto* mesh = stage.owner.AddComponent<MeshColliderComponent>();

    solid->SyncToPhysics(stage.physics);
    mesh->SyncToPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 1u);
    EXPECT_TRUE(mesh->BodyId().IsInvalid());
}

TEST(ColliderJolt, SlopeCreatesOneBody)
{
    ColliderStage stage;
    auto* slope = stage.owner.AddComponent<SlopeColliderComponent>();

    slope->SyncToPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 1u);
    EXPECT_FALSE(slope->BodyId().IsInvalid());
}

TEST(ColliderJolt, SyncingTwiceKeepsTheBodyId)
{
    ColliderStage stage;
    auto* box = stage.owner.AddComponent<BoxColliderComponent>();

    box->SyncToPhysics(stage.physics);
    const JPH::BodyID first = box->BodyId();
    box->SyncToPhysics(stage.physics);

    EXPECT_EQ(stage.physics.BodyCount(), 1u);
    EXPECT_EQ(box->BodyId(), first);
}

TEST(ColliderJolt, SyncingMovedBoxKeepsTheBodyIdAndMovesTheJoltBody)
{
    ColliderStage stage;
    auto* box = stage.owner.AddComponent<BoxColliderComponent>();

    box->SyncToPhysics(stage.physics);
    const JPH::BodyID first = box->BodyId();
    stage.owner.Root().SetPosition(Vector3{4.0f, 5.0f, 6.0f});
    box->SyncToPhysics(stage.physics);

    EXPECT_EQ(box->BodyId(), first);
    EXPECT_NEAR(stage.physics.BodyPosition(first).x, 4.0f, 1.0e-4f);
    EXPECT_NEAR(stage.physics.BodyPosition(first).y, 5.0f, 1.0e-4f);
    EXPECT_NEAR(stage.physics.BodyPosition(first).z, 6.0f, 1.0e-4f);
}

TEST(ColliderJolt, SyncingResizedBoxKeepsTheBodyIdAndUpdatesTheJoltShape)
{
    ColliderStage stage;
    auto* box = stage.owner.AddComponent<BoxColliderComponent>();

    box->SyncToPhysics(stage.physics);
    const JPH::BodyID first = box->BodyId();
    float distance = 0.0f;
    EXPECT_FALSE(stage.physics.Raycast(Vector3{1.5f, 2.0f, 0.0f}, Vector3{0.0f, -1.0f, 0.0f}, 4.0f, distance));

    box->SetHalfExtents(Vector3{2.0f, 0.5f, 0.5f});
    box->SyncToPhysics(stage.physics);

    EXPECT_EQ(box->BodyId(), first);
    EXPECT_TRUE(stage.physics.Raycast(Vector3{1.5f, 2.0f, 0.0f}, Vector3{0.0f, -1.0f, 0.0f}, 4.0f, distance));
}

// 寿命の終わりは持ち主の Scene の physics から外す。collider は physics を覚えていない
TEST(ColliderJolt, EndPlayRemovesItsOwnBody)
{
    NS::Obj::Scene scene;
    auto owned = std::make_unique<GameObject>();
    auto* box = owned->AddComponent<BoxColliderComponent>();
    scene.SpawnTransient(std::move(owned));

    box->SyncToPhysics(scene.Physics());
    box->OnEndPlay();

    EXPECT_EQ(scene.Physics().BodyCount(), 0u);
    EXPECT_TRUE(box->BodyId().IsInvalid());
}

// Scene に居る配置物の body は Scene の physics にだけ入る。別の physics に入ると、寿命の終わりに外しに行く先が違う
TEST(ColliderJolt, SyncIntoAPhysicsSceneOtherThanTheOwnersIsRefused)
{
    NS::Obj::Scene scene;
    auto owned = std::make_unique<GameObject>();
    auto* box = owned->AddComponent<BoxColliderComponent>();
    scene.SpawnTransient(std::move(owned));
    PhysicsScene other;

    box->SyncToPhysics(other);

    EXPECT_EQ(other.BodyCount(), 0u);
    EXPECT_TRUE(box->BodyId().IsInvalid());
}

// 別の physics から外そうとしても断る。断らないと、その physics が持たない id を消しに行って Jolt が落ちる
TEST(ColliderJolt, RemoveFromAPhysicsSceneOtherThanTheOwnersKeepsTheBody)
{
    NS::Obj::Scene scene;
    auto owned = std::make_unique<GameObject>();
    auto* box = owned->AddComponent<BoxColliderComponent>();
    scene.SpawnTransient(std::move(owned));
    box->SyncToPhysics(scene.Physics());
    PhysicsScene other;

    box->RemoveFromPhysics(other);

    EXPECT_EQ(scene.Physics().BodyCount(), 1u);
    EXPECT_FALSE(box->BodyId().IsInvalid());
}

// body を持たない collider は、Scene に居なくても OnEndPlay で PhysicsScene を触らない
TEST(ColliderJolt, EndPlayWithoutABodyLeavesThePhysicsSceneAlone)
{
    GameObject owner;
    auto* box = owner.AddComponent<BoxColliderComponent>();
    PhysicsScene physics;

    box->OnEndPlay();

    EXPECT_EQ(physics.BodyCount(), 0u);
    EXPECT_TRUE(box->BodyId().IsInvalid());
}

// 持ち主が Scene に居なければ、覚えている id がどの PhysicsScene の物か言えない
// 通すと、同じ index を配る別の PhysicsScene で無関係の body を作り変える
TEST(ColliderJolt, SyncWithoutAnOwningSceneIsRefused)
{
    GameObject owner;
    auto* box = owner.AddComponent<BoxColliderComponent>();
    PhysicsScene physics;

    box->SyncToPhysics(physics);

    EXPECT_EQ(physics.BodyCount(), 0u);
    EXPECT_TRUE(box->BodyId().IsInvalid());
}
