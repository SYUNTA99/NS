#include <Game/Entity/EntityComponent.h>
#include <Game/Entity/EntityEvents.h>
#include <Game/Player/PlayerComponent.h>
#include <Game/Player/PlayerEvents.h>
#include <Game/Player/PlayerStateManagerComponent.h>
#include <Game/Player/States/FallPlayerState.h>
#include <Game/Player/States/LedgeClimbingPlayerState.h>
#include <Game/Player/States/LedgeHangingPlayerState.h>
#include <Runtime/Core/AABB.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Transform.h>
#include <Runtime/Physics/PhysicsScene.h>

#include "entity_test_stage.h"
#include "jolt_test_scene.h"
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
    using NS::Game::Player::FallPlayerState;
    using NS::Game::Player::LedgeClimbingPlayerState;
    using NS::Game::Player::LedgeHangingPlayerState;
    using NS::Game::Player::PlayerComponent;
    using NS::Game::Player::PlayerEvents;
    using NS::Game::Player::PlayerStateManagerComponent;
    using NS::Obj::GameObject;

    constexpr float k_FixedDt = 1.0f / 60.0f;

    class FallingEntity final : public EntityComponent
    {
    protected:
        void HandleStates(float dt) override { Gravity(-25.0f, dt); }
    };

    AABB MakeBlock(float cx, float cy, float cz)
    {
        return AABB{Vector3{cx, cy, cz}, Vector3{0.5f, 0.5f, 0.5f}};
    }

    void AddFloor(NS::Phys::PhysicsScene& physics)
    {
        NsTest::AddBox(physics, AABB{Vector3{0.0f, -0.5f, 0.0f}, Vector3{64.0f, 0.5f, 64.0f}});
        physics.OptimizeBroadPhase();
    }

    FallingEntity& MakeAirborneEntity(GameObject& owner)
    {
        auto& entity = *owner.AddComponent<FallingEntity>();
        owner.Root().SetPosition(Vector3{0.0f, 2.0f, 0.0f});
        return entity;
    }

    PlayerComponent& MakeLedgeReady(GameObject& owner)
    {
        auto& manager = *owner.AddComponent<PlayerStateManagerComponent>();
        auto& player = *owner.AddComponent<PlayerComponent>();

        player.OnStart();
        manager.OnStart();
        // 立ちは縁掴みを持たず、立ちから始めると最初のフレームに掴まない
        // 状態機械は最初のフレームまで組まれないので、先に組んでから落下へ移す
        manager.EnsureBuilt(player);
        manager.Change<FallPlayerState>();
        return player;
    }

    PlayerComponent& MakeSlamReady(GameObject& owner, NS::Phys::PhysicsScene& physics)
    {
        auto& manager = *owner.AddComponent<PlayerStateManagerComponent>();
        auto& player = *owner.AddComponent<PlayerComponent>();

        AddFloor(physics);
        owner.Root().SetPosition(Vector3{0.0f, 1.0f, 0.0f});
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
    void SetUp() override { NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt); }
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
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Phys::PhysicsScene& physics = stage.physics;
    AddFloor(physics);
    auto& entity = MakeAirborneEntity(obj);

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
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Phys::PhysicsScene& physics = stage.physics;
    AddFloor(physics);
    auto& entity = MakeAirborneEntity(obj);
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
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Phys::PhysicsScene& physics = stage.physics;
    AddFloor(physics);
    auto& entity = MakeAirborneEntity(obj);
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
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Phys::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    int grabbed = 0;
    player.PlayerEventsRef().onLedgeGrabbed.Subscribe([&grabbed]() { ++grabbed; });

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();

    ASSERT_EQ(obj.FindComponent<PlayerStateManagerComponent>()->CurrentName(), LedgeHangingPlayerState::k_Name);
    EXPECT_EQ(grabbed, 1);
}

TEST_F(EntityEventsTest, LedgeClimbingFiresWhenTheClimbStarts)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Phys::PhysicsScene& physics = stage.physics;
    NsTest::AddBox(physics, MakeBlock(0.0f, 0.0f, 0.0f));
    physics.OptimizeBroadPhase();
    auto& player = MakeLedgeReady(obj);

    obj.Root().SetPosition(Vector3{-0.9f, 0.1f, 0.0f});
    player.SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.OnUpdate();
    player.OnUpdate();
    ASSERT_EQ(obj.FindComponent<PlayerStateManagerComponent>()->CurrentName(), LedgeHangingPlayerState::k_Name);

    int climbing = 0;
    player.PlayerEventsRef().onLedgeClimbing.Subscribe([&climbing]() { ++climbing; });

    player.SetDesiredMove(Vector3{0.0f, 0.0f, 0.0f}, 0.0f);
    player.SetClimbMove(0.0f, 1.0f);
    player.OnUpdate();

    ASSERT_EQ(obj.FindComponent<PlayerStateManagerComponent>()->CurrentName(), LedgeClimbingPlayerState::k_Name);
    EXPECT_EQ(climbing, 1);
}

TEST_F(EntityEventsTest, BodySlamStartedAndEndedFireAroundTheRush)
{
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Phys::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);

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
    NsTest::EntityStage stage;
    GameObject& obj = stage.owner;
    NS::Phys::PhysicsScene& physics = stage.physics;
    auto& player = MakeSlamReady(obj, physics);

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
