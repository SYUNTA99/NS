#include <Runtime/Core/Math.h>
#include <Runtime/Core/OBB.h>
#include <Runtime/Core/Sphere.h>
#include <Runtime/Object/Components/BoxCollider.h>
#include <Runtime/Object/Components/MeshCollider.h>
#include <Runtime/Object/Components/PhysicsSettings.h>
#include <Runtime/Object/Components/RigidBody.h>
#include <Runtime/Object/Components/SphereCollider.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/MeshCollision.h>
#include <Runtime/Physics/PhysicsScene.h>
#include <Runtime/Physics/ShapePart.h>
#include <Runtime/Platform/Clock.h>

#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace
{
    using NS::Core::OBB;
    using NS::Core::Quaternion;
    using NS::Core::Sphere;
    using NS::Core::Vector3;
    using NS::Obj::BoxCollider;
    using NS::Obj::GameObject;
    using NS::Obj::MeshCollider;
    using NS::Obj::PhysicsSettings;
    using NS::Obj::RigidBody;
    using NS::Obj::SphereCollider;
    using NS::Phys::BodyMotion;
    using NS::Phys::PhysicsScene;
    using NS::Phys::ShapePart;
    using NS::Phys::Triangle;

    constexpr float k_Tolerance = 1.0e-4f;

    OBB MakeBox(const Vector3& center, float half)
    {
        OBB box;
        box.center = center;
        box.halfExtentX = half;
        box.halfExtentY = half;
        box.halfExtentZ = half;
        return box;
    }

    std::vector<Triangle> MakeFloorQuad()
    {
        return {
            Triangle{Vector3{-1.0f, 0.0f, -1.0f}, Vector3{1.0f, 0.0f, 1.0f}, Vector3{1.0f, 0.0f, -1.0f}},
            Triangle{Vector3{-1.0f, 0.0f, -1.0f}, Vector3{-1.0f, 0.0f, 1.0f}, Vector3{1.0f, 0.0f, 1.0f}},
        };
    }

    // 箱 1 つを原点に置いた動く body。重力の確かめでは落ち方だけを見るので形は何でもよい
    JPH::BodyID AddMovingBox(PhysicsScene& physics, const Vector3& position, const BodyMotion& motion = {})
    {
        const ShapePart part = NS::Phys::MakeBoxPart(MakeBox(position, 0.5f));
        return physics.SyncMovingBody(JPH::BodyID{}, std::span<const ShapePart>{&part, 1}, position, Quaternion::Identity, motion);
    }

    void StepPhysics(PhysicsScene& physics, int steps)
    {
        for (int i = 0; i < steps; ++i)
        {
            physics.Update(NS::Platform::FrameTimer::FixedDelta());
        }
    }

    // Scene を 1 歩ずつ回す。RigidBody の前後の処理は Scene::OnUpdate だけが呼ぶので、物理だけを回しても書き戻らない
    struct RigidStage
    {
        NS::Obj::Scene scene;
        PhysicsScene& physics = scene.Physics();

        // 部品を積んでから湧かす。湧かした時点で OnStart が走り、RigidBody が形を集める
        GameObject& Spawn(std::unique_ptr<GameObject> obj) { return *scene.SpawnTransient(std::move(obj)); }

        void Step(int steps)
        {
            for (int i = 0; i < steps; ++i)
            {
                scene.OnUpdate();
            }
        }
    };

    std::unique_ptr<GameObject> MakeObjectAt(const Vector3& position)
    {
        std::unique_ptr<GameObject> obj = std::make_unique<GameObject>();
        obj->Root().SetPosition(position);
        return obj;
    }

    // 上面が y = 0.5 の広い床
    GameObject& SpawnFloor(RigidStage& stage)
    {
        std::unique_ptr<GameObject> floor = MakeObjectAt(Vector3{0.0f, 0.0f, 0.0f});
        floor->AddComponent<BoxCollider>(Vector3{10.0f, 0.5f, 10.0f});
        GameObject& spawned = stage.Spawn(std::move(floor));
        // 静的な collider は湧いただけでは入らない。本番と同じ張り直しの口を通す
        stage.scene.SyncPhysics();
        return spawned;
    }
} // namespace

// ---- PhysicsScene ----

TEST(PhysicsGravity, StartsAtDefaultGravity)
{
    PhysicsScene physics;

    const Vector3 gravity = physics.Gravity();

    EXPECT_FLOAT_EQ(gravity.x, 0.0f);
    EXPECT_FLOAT_EQ(gravity.y, NS::Phys::k_DefaultGravityY);
    EXPECT_FLOAT_EQ(gravity.z, 0.0f);
}

TEST(PhysicsGravity, SetGravityChangesHowBodiesFall)
{
    PhysicsScene physics;
    const JPH::BodyID body = AddMovingBox(physics, Vector3{0.0f, 10.0f, 0.0f});
    physics.SetGravity(Vector3{4.0f, 0.0f, 0.0f});

    StepPhysics(physics, 30);

    EXPECT_NEAR(physics.BodyPosition(body).y, 10.0f, k_Tolerance);
    EXPECT_GT(physics.BodyPosition(body).x, 0.0f);
}

TEST(PhysicsGravity, SetGravityIgnoresNonFiniteValues)
{
    PhysicsScene physics;
    physics.SetGravity(Vector3{0.0f, -9.8f, 0.0f});

    physics.SetGravity(Vector3{0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f});

    EXPECT_FLOAT_EQ(physics.Gravity().y, -9.8f);
}

TEST(PhysicsMovingBody, TwoPartsMakeOneBodyAtTheOrigin)
{
    PhysicsScene physics;
    Sphere sphere;
    sphere.center = Vector3{0.0f, 2.0f, 0.0f};
    sphere.radius = 0.5f;
    const ShapePart parts[] = {NS::Phys::MakeBoxPart(MakeBox(Vector3{0.0f, 0.0f, 0.0f}, 0.5f)),
                               NS::Phys::MakeSpherePart(sphere)};

    const JPH::BodyID body =
        physics.SyncMovingBody(JPH::BodyID{}, parts, Vector3{0.0f, 0.0f, 0.0f}, Quaternion::Identity, BodyMotion{});

    ASSERT_FALSE(body.IsInvalid());
    EXPECT_EQ(physics.BodyCount(), 1u);
    // 重心は球の側へ寄るが、body の位置は渡した原点のまま
    EXPECT_NEAR(physics.BodyPosition(body).y, 0.0f, k_Tolerance);
}

TEST(PhysicsMovingBody, NoPartsMakeNoBody)
{
    PhysicsScene physics;

    const JPH::BodyID body =
        physics.SyncMovingBody(JPH::BodyID{}, {}, Vector3{0.0f, 0.0f, 0.0f}, Quaternion::Identity, BodyMotion{});

    EXPECT_TRUE(body.IsInvalid());
    EXPECT_EQ(physics.BodyCount(), 0u);
}

TEST(PhysicsMovingBody, SkipsPartsThatMustBeStatic)
{
    PhysicsScene physics;
    const ShapePart mesh{NS::Phys::CreateMeshShape(MakeFloorQuad()), Vector3{0.0f, 0.0f, 0.0f}, Quaternion::Identity};
    ASSERT_NE(mesh.shape, nullptr);

    const JPH::BodyID body = physics.SyncMovingBody(
        JPH::BodyID{}, std::span<const ShapePart>{&mesh, 1}, Vector3{0.0f, 0.0f, 0.0f}, Quaternion::Identity, BodyMotion{});

    EXPECT_TRUE(body.IsInvalid());
}

TEST(PhysicsMovingBody, ReplacesAStaticBodyWithoutRemovingIt)
{
    PhysicsScene physics;
    const JPH::BodyID stationary = physics.AddBox(MakeBox(Vector3{0.0f, 0.0f, 0.0f}, 0.5f), NS::Phys::ObjectLayers::Terrain);
    const ShapePart part = NS::Phys::MakeBoxPart(MakeBox(Vector3{0.0f, 0.0f, 0.0f}, 0.5f));

    const JPH::BodyID moving = physics.SyncMovingBody(
        stationary, std::span<const ShapePart>{&part, 1}, Vector3{0.0f, 0.0f, 0.0f}, Quaternion::Identity, BodyMotion{});

    EXPECT_NE(moving, stationary);
    EXPECT_EQ(physics.BodyCount(), 2u);
}

TEST(PhysicsMovingBody, ResyncKeepsTheSameId)
{
    PhysicsScene physics;
    const JPH::BodyID first = AddMovingBox(physics, Vector3{0.0f, 0.0f, 0.0f});
    const ShapePart part = NS::Phys::MakeBoxPart(MakeBox(Vector3{3.0f, 0.0f, 0.0f}, 1.0f));

    const JPH::BodyID second = physics.SyncMovingBody(
        first, std::span<const ShapePart>{&part, 1}, Vector3{3.0f, 0.0f, 0.0f}, Quaternion::Identity, BodyMotion{});

    EXPECT_EQ(second, first);
    EXPECT_EQ(physics.BodyCount(), 1u);
    EXPECT_NEAR(physics.BodyPosition(second).x, 3.0f, k_Tolerance);
}

// 浮く側の確かめの対照。既定の動き方なら同じ置き方で落ちる
TEST(PhysicsMovingBody, DefaultMotionFalls)
{
    PhysicsScene physics;
    const JPH::BodyID body = AddMovingBox(physics, Vector3{0.0f, 10.0f, 0.0f});

    StepPhysics(physics, 30);

    EXPECT_LT(physics.BodyPosition(body).y, 9.0f);
}

TEST(PhysicsMovingBody, ZeroGravityFactorFloats)
{
    PhysicsScene physics;
    BodyMotion motion;
    motion.gravityFactor = 0.0f;
    const JPH::BodyID body = AddMovingBox(physics, Vector3{0.0f, 10.0f, 0.0f}, motion);

    StepPhysics(physics, 30);

    EXPECT_NEAR(physics.BodyPosition(body).y, 10.0f, k_Tolerance);
}

TEST(PhysicsMovingBody, KinematicIgnoresGravity)
{
    PhysicsScene physics;
    const JPH::BodyID body = AddMovingBox(physics, Vector3{0.0f, 10.0f, 0.0f});
    BodyMotion motion;
    motion.kinematic = true;

    physics.SetBodyMotion(body, motion);
    StepPhysics(physics, 30);

    EXPECT_NEAR(physics.BodyPosition(body).y, 10.0f, k_Tolerance);
}

TEST(PhysicsMovingBody, MoveKinematicArrivesAfterOneUpdate)
{
    PhysicsScene physics;
    BodyMotion motion;
    motion.kinematic = true;
    const JPH::BodyID body = AddMovingBox(physics, Vector3{0.0f, 0.0f, 0.0f}, motion);
    const float dt = NS::Platform::FrameTimer::FixedDelta();

    physics.MoveKinematic(body, Vector3{1.0f, 0.0f, 0.0f}, Quaternion::Identity, dt);
    physics.Update(dt);

    EXPECT_NEAR(physics.BodyPosition(body).x, 1.0f, 1.0e-3f);
}

TEST(PhysicsMovingBody, ImpulseChangesVelocityByMass)
{
    PhysicsScene physics;
    physics.SetGravity(Vector3{0.0f, 0.0f, 0.0f});
    BodyMotion motion;
    motion.mass = 2.0f;
    const JPH::BodyID body = AddMovingBox(physics, Vector3{0.0f, 0.0f, 0.0f}, motion);

    physics.AddBodyImpulse(body, Vector3{4.0f, 0.0f, 0.0f});

    EXPECT_NEAR(physics.BodyVelocity(body).x, 2.0f, k_Tolerance);
}

TEST(PhysicsMovingBody, LockedTranslationYDoesNotFall)
{
    PhysicsScene physics;
    BodyMotion motion;
    motion.allowedDOFs = JPH::EAllowedDOFs::All & ~JPH::EAllowedDOFs::TranslationY;
    const JPH::BodyID body = AddMovingBox(physics, Vector3{0.0f, 10.0f, 0.0f}, motion);

    StepPhysics(physics, 30);

    EXPECT_NEAR(physics.BodyPosition(body).y, 10.0f, k_Tolerance);
}

// 全軸を塞いだ動的 body は Jolt が 0 で割って落ちる。キネマティックとして作るので止まったまま回る
TEST(PhysicsMovingBody, AllAxesLockedStaysPut)
{
    PhysicsScene physics;
    BodyMotion motion;
    motion.allowedDOFs = JPH::EAllowedDOFs::None;
    const JPH::BodyID body = AddMovingBox(physics, Vector3{0.0f, 10.0f, 0.0f}, motion);

    StepPhysics(physics, 30);

    ASSERT_FALSE(body.IsInvalid());
    EXPECT_NEAR(physics.BodyPosition(body).y, 10.0f, k_Tolerance);
}

// ---- RigidBody ----

TEST(RigidBody, GathersCollidersIntoOneBody)
{
    RigidStage stage;
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 5.0f, 0.0f});
    BoxCollider* box = obj->AddComponent<BoxCollider>();
    SphereCollider* sphere = obj->AddComponent<SphereCollider>();
    sphere->SetCenterOffset(Vector3{0.0f, 1.0f, 0.0f});
    RigidBody* body = obj->AddComponent<RigidBody>();
    const JPH::uint before = stage.physics.BodyCount();

    stage.Spawn(std::move(obj));

    ASSERT_FALSE(body->BodyId().IsInvalid());
    EXPECT_EQ(stage.physics.BodyCount(), before + 1u);
    EXPECT_EQ(box->BodyId(), body->BodyId());
    EXPECT_EQ(sphere->BodyId(), body->BodyId());
}

TEST(RigidBody, SyncPhysicsKeepsCollidersInsideTheBody)
{
    RigidStage stage;
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 5.0f, 0.0f});
    BoxCollider* box = obj->AddComponent<BoxCollider>();
    RigidBody* body = obj->AddComponent<RigidBody>();
    stage.Spawn(std::move(obj));
    const JPH::BodyID first = body->BodyId();
    const JPH::uint before = stage.physics.BodyCount();

    stage.scene.SyncPhysics();

    EXPECT_EQ(body->BodyId(), first);
    EXPECT_EQ(box->BodyId(), first);
    EXPECT_EQ(stage.physics.BodyCount(), before);
}

TEST(RigidBody, BodyOriginIsTheOwnerNotTheColliderOffset)
{
    RigidStage stage;
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{2.0f, 5.0f, 0.0f});
    BoxCollider* box = obj->AddComponent<BoxCollider>();
    box->SetCenterOffset(Vector3{0.0f, 1.0f, 0.0f});
    RigidBody* body = obj->AddComponent<RigidBody>();
    body->SetUseGravity(false);
    GameObject& owner = stage.Spawn(std::move(obj));

    stage.Step(10);

    const Vector3 bodyPosition = stage.physics.BodyPosition(body->BodyId());
    EXPECT_NEAR(bodyPosition.x, 2.0f, k_Tolerance);
    EXPECT_NEAR(bodyPosition.y, 5.0f, k_Tolerance);
    // 重力を受けない間は、書き戻しを繰り返しても持ち主が動かない
    EXPECT_NEAR(owner.Root().Position().y, 5.0f, k_Tolerance);
}

TEST(RigidBody, DynamicBodyFallsAndWritesTheTransform)
{
    RigidStage stage;
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 10.0f, 0.0f});
    obj->AddComponent<BoxCollider>();
    RigidBody* body = obj->AddComponent<RigidBody>();
    GameObject& owner = stage.Spawn(std::move(obj));

    stage.Step(30);

    EXPECT_LT(owner.Root().Position().y, 10.0f);
    EXPECT_NEAR(owner.Root().Position().y, stage.physics.BodyPosition(body->BodyId()).y, k_Tolerance);
    EXPECT_LT(body->Velocity().y, 0.0f);
}

TEST(RigidBody, LandsOnAStaticFloor)
{
    RigidStage stage;
    SpawnFloor(stage);
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 3.0f, 0.0f});
    obj->AddComponent<BoxCollider>();
    obj->AddComponent<RigidBody>();
    GameObject& owner = stage.Spawn(std::move(obj));

    stage.Step(180);

    // 床の上面 0.5 に半径 0.5 の箱が乗る
    EXPECT_NEAR(owner.Root().Position().y, 1.0f, 0.05f);
}

TEST(RigidBody, OwnerScaleIsBakedIntoTheShape)
{
    RigidStage stage;
    SpawnFloor(stage);
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 4.0f, 0.0f});
    obj->Root().SetScale(Vector3{2.0f, 2.0f, 2.0f});
    obj->AddComponent<BoxCollider>();
    obj->AddComponent<RigidBody>();
    GameObject& owner = stage.Spawn(std::move(obj));

    stage.Step(180);

    EXPECT_NEAR(owner.Root().Position().y, 1.5f, 0.05f);
    EXPECT_FLOAT_EQ(owner.Root().Scale().y, 2.0f);
}

TEST(RigidBody, WithoutGravityStaysPut)
{
    RigidStage stage;
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 10.0f, 0.0f});
    obj->AddComponent<BoxCollider>();
    obj->AddComponent<RigidBody>()->SetUseGravity(false);
    GameObject& owner = stage.Spawn(std::move(obj));

    stage.Step(30);

    EXPECT_NEAR(owner.Root().Position().y, 10.0f, k_Tolerance);
}

TEST(RigidBody, FieldChangesReachTheBodyOnTheNextStep)
{
    RigidStage stage;
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 10.0f, 0.0f});
    obj->AddComponent<BoxCollider>();
    RigidBody* body = obj->AddComponent<RigidBody>();
    body->SetUseGravity(false);
    GameObject& owner = stage.Spawn(std::move(obj));

    body->SetUseGravity(true);
    stage.Step(30);

    EXPECT_LT(owner.Root().Position().y, 10.0f);
}

TEST(RigidBody, KinematicFollowsTheTransform)
{
    RigidStage stage;
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 10.0f, 0.0f});
    obj->AddComponent<BoxCollider>();
    RigidBody* body = obj->AddComponent<RigidBody>();
    body->SetKinematic(true);
    GameObject& owner = stage.Spawn(std::move(obj));

    owner.Root().SetPosition(Vector3{2.0f, 10.0f, 0.0f});
    stage.Step(1);

    EXPECT_NEAR(stage.physics.BodyPosition(body->BodyId()).x, 2.0f, 1.0e-3f);
    EXPECT_NEAR(stage.physics.BodyPosition(body->BodyId()).y, 10.0f, 1.0e-3f);
    EXPECT_FLOAT_EQ(owner.Root().Position().x, 2.0f);
}

TEST(RigidBody, SceneGravityComesFromPhysicsSettings)
{
    RigidStage stage;
    std::unique_ptr<GameObject> settings = std::make_unique<GameObject>();
    settings->AddComponent<PhysicsSettings>()->SetGravity(Vector3{0.0f, 0.0f, 0.0f});
    stage.Spawn(std::move(settings));
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 10.0f, 0.0f});
    obj->AddComponent<BoxCollider>();
    obj->AddComponent<RigidBody>();
    GameObject& owner = stage.Spawn(std::move(obj));

    stage.Step(30);

    EXPECT_NEAR(stage.physics.Gravity().y, 0.0f, k_Tolerance);
    EXPECT_NEAR(owner.Root().Position().y, 10.0f, k_Tolerance);
}

TEST(RigidBody, PhysicsSettingsEditsApplyWhileRunning)
{
    RigidStage stage;
    std::unique_ptr<GameObject> settings = std::make_unique<GameObject>();
    PhysicsSettings* world = settings->AddComponent<PhysicsSettings>();
    stage.Spawn(std::move(settings));

    world->SetGravity(Vector3{0.0f, -3.0f, 0.0f});
    stage.Step(1);

    EXPECT_FLOAT_EQ(stage.physics.Gravity().y, -3.0f);
}

TEST(RigidBody, DisablingItGivesCollidersTheirOwnStaticBodies)
{
    RigidStage stage;
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 5.0f, 0.0f});
    BoxCollider* box = obj->AddComponent<BoxCollider>();
    SphereCollider* sphere = obj->AddComponent<SphereCollider>();
    RigidBody* body = obj->AddComponent<RigidBody>();
    stage.Spawn(std::move(obj));
    const JPH::uint withBody = stage.physics.BodyCount();

    body->SetActive(false);
    stage.scene.SyncPhysics();

    EXPECT_TRUE(body->BodyId().IsInvalid());
    EXPECT_EQ(stage.physics.BodyCount(), withBody + 1u);
    EXPECT_FALSE(box->BodyId().IsInvalid());
    EXPECT_FALSE(sphere->BodyId().IsInvalid());
    EXPECT_NE(box->BodyId(), sphere->BodyId());
}

TEST(RigidBody, TriggerBoxKeepsItsSensorAndFollows)
{
    RigidStage stage;
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 10.0f, 0.0f});
    obj->AddComponent<SphereCollider>();
    BoxCollider* trigger = obj->AddComponent<BoxCollider>();
    trigger->SetTrigger(true);
    RigidBody* body = obj->AddComponent<RigidBody>();
    GameObject& owner = stage.Spawn(std::move(obj));
    stage.scene.SyncPhysics();

    stage.Step(30);

    ASSERT_FALSE(trigger->BodyId().IsInvalid());
    EXPECT_NE(trigger->BodyId(), body->BodyId());
    EXPECT_NEAR(stage.physics.BodyPosition(trigger->BodyId()).y, owner.Root().Position().y, k_Tolerance);
    EXPECT_LT(owner.Root().Position().y, 10.0f);
}

TEST(RigidBody, MeshColliderStaysStatic)
{
    NS::Phys::MeshCollision floor{MakeFloorQuad(), nullptr};
    floor.shape = NS::Phys::CreateMeshShape(floor.triangles);
    RigidStage stage;
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 5.0f, 0.0f});
    obj->AddComponent<SphereCollider>();
    MeshCollider* mesh = obj->AddComponent<MeshCollider>();
    mesh->SetCollision(&floor);
    RigidBody* body = obj->AddComponent<RigidBody>();
    stage.Spawn(std::move(obj));

    stage.scene.SyncPhysics();

    ASSERT_FALSE(body->BodyId().IsInvalid());
    ASSERT_FALSE(mesh->BodyId().IsInvalid());
    EXPECT_NE(mesh->BodyId(), body->BodyId());
}

TEST(RigidBody, WithoutCollidersMakesNoBody)
{
    RigidStage stage;
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 5.0f, 0.0f});
    RigidBody* body = obj->AddComponent<RigidBody>();
    const JPH::uint before = stage.physics.BodyCount();

    stage.Spawn(std::move(obj));

    EXPECT_TRUE(body->BodyId().IsInvalid());
    EXPECT_EQ(stage.physics.BodyCount(), before);
}

TEST(RigidBody, TeleportMovesTransformAndBody)
{
    RigidStage stage;
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 10.0f, 0.0f});
    obj->AddComponent<BoxCollider>();
    RigidBody* body = obj->AddComponent<RigidBody>();
    GameObject& owner = stage.Spawn(std::move(obj));
    stage.Step(10);

    body->Teleport(Vector3{5.0f, 20.0f, 0.0f}, Quaternion::Identity);

    EXPECT_NEAR(owner.Root().Position().x, 5.0f, k_Tolerance);
    EXPECT_NEAR(stage.physics.BodyPosition(body->BodyId()).x, 5.0f, k_Tolerance);
    EXPECT_NEAR(stage.physics.BodyPosition(body->BodyId()).y, 20.0f, k_Tolerance);
}

TEST(RigidBody, ChildWritesBackItsLocalPose)
{
    RigidStage stage;
    std::unique_ptr<GameObject> parent = MakeObjectAt(Vector3{10.0f, 0.0f, 0.0f});
    GameObject& parentRef = stage.Spawn(std::move(parent));
    std::unique_ptr<GameObject> child = MakeObjectAt(Vector3{0.0f, 5.0f, 0.0f});
    child->SetParent(&parentRef);
    child->AddComponent<BoxCollider>();
    RigidBody* body = child->AddComponent<RigidBody>();
    GameObject& childRef = stage.Spawn(std::move(child));

    stage.Step(30);

    // 落ちるのは世界の真下で、親から見た X は 0 のまま
    EXPECT_NEAR(childRef.Root().Position().x, 0.0f, k_Tolerance);
    EXPECT_LT(childRef.Root().Position().y, 5.0f);
    EXPECT_NEAR(stage.physics.BodyPosition(body->BodyId()).x, 10.0f, k_Tolerance);
}

TEST(RigidBody, ImpulseMovesTheOwner)
{
    RigidStage stage;
    std::unique_ptr<GameObject> obj = MakeObjectAt(Vector3{0.0f, 10.0f, 0.0f});
    obj->AddComponent<BoxCollider>();
    RigidBody* body = obj->AddComponent<RigidBody>();
    body->SetUseGravity(false);
    GameObject& owner = stage.Spawn(std::move(obj));

    body->AddImpulse(Vector3{3.0f, 0.0f, 0.0f});
    stage.Step(30);

    EXPECT_GT(owner.Root().Position().x, 1.0f);
}
