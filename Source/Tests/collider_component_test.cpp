#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/BoxColliderComponent.h>
#include <Runtime/Object/Components/CapsuleColliderComponent.h>
#include <Runtime/Object/Components/MeshColliderComponent.h>
#include <Runtime/Object/Components/SlopeColliderComponent.h>
#include <Runtime/Object/Components/SphereColliderComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/JoltWorld.h>
#include <Runtime/Physics/SweptTriangle.h>

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
    using NS::Physics::JoltWorld;
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
    JoltWorld world;

    box->AddToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_FALSE(box->BodyId().IsInvalid());
    EXPECT_NEAR(world.BodyPosition(box->BodyId()).y, 3.0f, 1.0e-4f);
}

TEST(ColliderJolt, TriggerBoxCreatesNoBody)
{
    GameObject owner;
    auto* box = owner.AddComponent<BoxColliderComponent>();
    box->SetTrigger(true);
    JoltWorld world;

    box->AddToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 0u);
    EXPECT_TRUE(box->BodyId().IsInvalid());
}

TEST(ColliderJolt, SphereCreatesOneBody)
{
    GameObject owner;
    auto* sphere = owner.AddComponent<SphereColliderComponent>();
    sphere->SetRadius(0.75f);
    JoltWorld world;

    sphere->AddToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_FALSE(sphere->BodyId().IsInvalid());
}

TEST(ColliderJolt, CapsuleCreatesOneBody)
{
    GameObject owner;
    auto* capsule = owner.AddComponent<CapsuleColliderComponent>();
    JoltWorld world;

    capsule->AddToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_FALSE(capsule->BodyId().IsInvalid());
}

TEST(ColliderJolt, ExcludedCapsuleCreatesNoBody)
{
    GameObject owner;
    auto* capsule = owner.AddComponent<CapsuleColliderComponent>();
    capsule->SetExcludedFromStaticWorld(true);
    JoltWorld world;

    capsule->AddToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 0u);
    EXPECT_TRUE(capsule->BodyId().IsInvalid());
}

TEST(ColliderJolt, MeshCreatesOneBodyForAllTriangles)
{
    GameObject owner;
    auto* mesh = owner.AddComponent<MeshColliderComponent>();
    mesh->SetLocalTriangles(MakeFloorQuad());
    JoltWorld world;

    mesh->AddToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_FALSE(mesh->BodyId().IsInvalid());
}

TEST(ColliderJolt, EmptyMeshCreatesNoBody)
{
    GameObject owner;
    auto* mesh = owner.AddComponent<MeshColliderComponent>();
    JoltWorld world;

    mesh->AddToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 0u);
    EXPECT_TRUE(mesh->BodyId().IsInvalid());
}

TEST(ColliderJolt, SlopeCreatesOneBody)
{
    GameObject owner;
    auto* slope = owner.AddComponent<SlopeColliderComponent>();
    JoltWorld world;

    slope->AddToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_FALSE(slope->BodyId().IsInvalid());
}

TEST(ColliderJolt, AddingTwiceReplacesTheBody)
{
    GameObject owner;
    auto* box = owner.AddComponent<BoxColliderComponent>();
    JoltWorld world;

    box->AddToPhysics(world);
    const JPH::BodyID first = box->BodyId();
    box->AddToPhysics(world);

    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_NE(box->BodyId(), first);
}
