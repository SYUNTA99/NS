#include <Game/Entity/EntityComponent.h>
#include <Game/Entity/EntityEvents.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerEvents.h>
#include <Game/Player/PlayerStateManagerComponent.h>
#include <Runtime/Core/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/PhysicsWorld.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <functional>

namespace
{
    using NS::Core::AABB;
    using NS::Core::Vector3;
    using NS::Game::Entity::EntityComponent;
    using NS::Game::Entity::EntityEvent;
    using NS::Game::Entity::EntityEvents;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerEvents;
    using NS::Game::Player::PlayerStateManagerComponent;
    using NS::Object::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;

    class FallingEntity final : public EntityComponent
    {
    protected:
        void HandleStates(float dt) override
        {
            Gravity(-25.0f, dt);
            Move(dt);
        }
    };

    AABB MakeBlock(float cx, float cy, float cz)
    {
        return AABB{Vector3{cx, cy, cz}, Vector3{0.5f, 0.5f, 0.5f}};
    }

    void AddFloor(NS::Physics::PhysicsWorld& world)
    {
        world.AddAABB(AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{64.0f, 0.5f, 64.0f}});
        world.BuildBroadphase();
    }

    FallingEntity& MakeAirborneEntity(GameObject& owner, NS::Physics::PhysicsWorld& world)
    {
        auto& entity = *owner.AddComponent<FallingEntity>();
        owner.Root().SetPosition(Vector3{0.0f, 2.0f, 0.0f});
        entity.SetPhysicsWorld(&world);
        return entity;
    }

    PlayerComponent& MakeLedgeReady(GameObject& owner, NS::Physics::PhysicsWorld& world)
    {
        auto& manager = *owner.AddComponent<PlayerStateManagerComponent>();
        auto& player = *owner.AddComponent<PlayerComponent>();

        player.SetPhysicsWorld(&world);
        player.SetDebugDrawEnabled(false);
        player.OnStart();
        manager.OnStart();
        return player;
    }

    PlayerComponent& MakeSlamReady(GameObject& owner, NS::Physics::PhysicsWorld& world)
    {
        auto& manager = *owner.AddComponent<PlayerStateManagerComponent>();
        auto& player = *owner.AddComponent<PlayerComponent>();

        AddFloor(world);
        owner.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});
        player.SetPhysicsWorld(&world);
        player.SetDebugDrawEnabled(false);
        player.OnStart();
        manager.OnStart();

        for (int i = 0; i < 30 && !player.IsGrounded(); ++i)
            player.OnUpdate();
        return player;
    }
} // namespace

class EntityEventsTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(k_FixedDt); }
};

TEST_F(EntityEventsTest, InvokeWithoutSubscriberDoesNothing)
{
    EntityEvent ev;

    ev.Invoke();

    EXPECT_EQ(ev.SubscriberCount(), std::size_t{0});
}

TEST_F(EntityEventsTest, SingleSubscriberIsCalledOnce)
{
    EntityEvent ev;
    int count = 0;
    ev.Subscribe([&count]() { ++count; });

    ev.Invoke();

    EXPECT_EQ(count, 1);
}

TEST_F(EntityEventsTest, TwoSubscribersAreBothCalled)
{
    EntityEvent ev;
    int first = 0;
    int second = 0;
    ev.Subscribe([&first]() { ++first; });
    ev.Subscribe([&second]() { ++second; });

    ev.Invoke();

    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 1);
    EXPECT_EQ(ev.SubscriberCount(), std::size_t{2});
}

TEST_F(EntityEventsTest, SubscriberCountMatchesTheNumberAdded)
{
    EntityEvent ev;

    EXPECT_EQ(ev.SubscriberCount(), std::size_t{0});
    ev.Subscribe([]() {});
    EXPECT_EQ(ev.SubscriberCount(), std::size_t{1});
    ev.Subscribe([]() {});
    EXPECT_EQ(ev.SubscriberCount(), std::size_t{2});
}

TEST_F(EntityEventsTest, EmptyCallbackIsDropped)
{
    EntityEvent ev;

    ev.Subscribe(std::function<void()>{});
    ev.Invoke();

    EXPECT_EQ(ev.SubscriberCount(), std::size_t{0});
}

TEST_F(EntityEventsTest, GroundEnterAndGroundExitAreSeparate)
{
    EntityEvents events;
    int entered = 0;
    int exited = 0;
    events.onGroundEnter.Subscribe([&entered]() { ++entered; });
    events.onGroundExit.Subscribe([&exited]() { ++exited; });

    events.onGroundEnter.Invoke();

    EXPECT_EQ(entered, 1);
    EXPECT_EQ(exited, 0);
}

TEST_F(EntityEventsTest, PlayerNotificationsAreSeparate)
{
    PlayerEvents events;
    int jumped = 0;
    events.onJump.Subscribe([&jumped]() { ++jumped; });

    events.onJump.Invoke();
    events.onLedgeGrabbed.Invoke();
    events.onLedgeClimbing.Invoke();
    events.onBodySlamStarted.Invoke();
    events.onBodySlamEnded.Invoke();

    EXPECT_EQ(jumped, 1);
}

TEST_F(EntityEventsTest, GroundEnterFiresOnceOnTheLandingStep)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    AddFloor(world);
    auto& entity = MakeAirborneEntity(obj, world);

    int entered = 0;
    int exited = 0;
    entity.Events().onGroundEnter.Subscribe([&entered]() { ++entered; });
    entity.Events().onGroundExit.Subscribe([&exited]() { ++exited; });

    for (int i = 0; i < 60; ++i)
        entity.OnUpdate();

    ASSERT_TRUE(entity.IsGrounded());
    EXPECT_EQ(entered, 1);
    EXPECT_EQ(exited, 0);
}

TEST_F(EntityEventsTest, GroundExitFiresOnceOnTheLeavingStep)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    AddFloor(world);
    auto& entity = MakeAirborneEntity(obj, world);
    for (int i = 0; i < 60; ++i)
        entity.OnUpdate();
    ASSERT_TRUE(entity.IsGrounded());

    int entered = 0;
    int exited = 0;
    entity.Events().onGroundEnter.Subscribe([&entered]() { ++entered; });
    entity.Events().onGroundExit.Subscribe([&exited]() { ++exited; });

    entity.SetVerticalVelocity(12.0f);
    for (int i = 0; i < 5; ++i)
        entity.OnUpdate();

    ASSERT_FALSE(entity.IsGrounded());
    EXPECT_EQ(exited, 1);
    EXPECT_EQ(entered, 0);
}

TEST_F(EntityEventsTest, StayingGroundedFiresNeitherNotification)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    AddFloor(world);
    auto& entity = MakeAirborneEntity(obj, world);
    for (int i = 0; i < 60; ++i)
        entity.OnUpdate();
    ASSERT_TRUE(entity.IsGrounded());

    int entered = 0;
    int exited = 0;
    entity.Events().onGroundEnter.Subscribe([&entered]() { ++entered; });
    entity.Events().onGroundExit.Subscribe([&exited]() { ++exited; });

    for (int i = 0; i < 10; ++i)
        entity.OnUpdate();

    EXPECT_EQ(entered, 0);
    EXPECT_EQ(exited, 0);
}

TEST_F(EntityEventsTest, JumpNotificationFiresOnlyWhenTheJumpHappens)
{
    GameObject obj;
    auto& player = *obj.AddComponent<PlayerComponent>();
    player.SetDebugDrawEnabled(false);

    int jumped = 0;
    player.PlayerEventsRef().onJump.Subscribe([&jumped]() { ++jumped; });

    player.SetGrounded(true);
    player.Jump(k_FixedDt);
    EXPECT_EQ(jumped, 0);

    player.SetJumpPressed();
    player.Jump(k_FixedDt);
    EXPECT_EQ(jumped, 1);
}

TEST_F(EntityEventsTest, LedgeGrabbedFiresOnTheGrabbingStep)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    world.AddAABB(MakeBlock(0.0f, 0.0f, 0.0f));
    world.BuildBroadphase();
    auto& player = MakeLedgeReady(obj, world);

    int grabbed = 0;
    player.PlayerEventsRef().onLedgeGrabbed.Subscribe([&grabbed]() { ++grabbed; });

    obj.Root().SetPosition(Vector3{-0.9f, 0.0f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();

    ASSERT_EQ(obj.FindComponent<PlayerStateManagerComponent>()->CurrentName(),
              PlayerComponent::k_LedgeHangingStateName);
    EXPECT_EQ(grabbed, 1);
}

TEST_F(EntityEventsTest, LedgeClimbingFiresWhenTheClimbStarts)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    world.AddAABB(MakeBlock(0.0f, 0.0f, 0.0f));
    world.BuildBroadphase();
    auto& player = MakeLedgeReady(obj, world);

    obj.Root().SetPosition(Vector3{-0.9f, 0.0f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    ASSERT_EQ(obj.FindComponent<PlayerStateManagerComponent>()->CurrentName(),
              PlayerComponent::k_LedgeHangingStateName);

    int climbing = 0;
    player.PlayerEventsRef().onLedgeClimbing.Subscribe([&climbing]() { ++climbing; });

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetJumpPressed();
    player.OnUpdate();

    ASSERT_EQ(obj.FindComponent<PlayerStateManagerComponent>()->CurrentName(),
              PlayerComponent::k_LedgeClimbingStateName);
    EXPECT_EQ(climbing, 1);
}

TEST_F(EntityEventsTest, BodySlamStartedAndEndedFireAroundTheRush)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& player = MakeSlamReady(obj, world);

    int started = 0;
    int ended = 0;
    player.PlayerEventsRef().onBodySlamStarted.Subscribe([&started]() { ++started; });
    player.PlayerEventsRef().onBodySlamEnded.Subscribe([&ended]() { ++ended; });

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();

    ASSERT_TRUE(player.IsBodySlamming());
    EXPECT_EQ(started, 1);
    EXPECT_EQ(ended, 0);

    for (int i = 0; i < 120 && player.IsBodySlamming(); ++i)
        player.OnUpdate();

    ASSERT_FALSE(player.IsBodySlamming());
    EXPECT_EQ(started, 1);
    EXPECT_EQ(ended, 1);
}

TEST_F(EntityEventsTest, CancelBodySlamFiresTheEndNotification)
{
    GameObject obj;
    NS::Physics::PhysicsWorld world;
    auto& player = MakeSlamReady(obj, world);

    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.RequestBodySlam(1.0f);
    player.OnUpdate();
    ASSERT_TRUE(player.IsBodySlamming());

    int ended = 0;
    player.PlayerEventsRef().onBodySlamEnded.Subscribe([&ended]() { ++ended; });

    player.CancelBodySlam();

    ASSERT_FALSE(player.IsBodySlamming());
    EXPECT_EQ(ended, 1);
}
