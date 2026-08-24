#include <Game/Entity/EntityEvents.h>
#include <Game/Player/PlayerEvents.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <functional>

namespace
{
    using NS::Game::Entity::EntityEvent;
    using NS::Game::Entity::EntityEvents;
    using NS::Game::Player::PlayerEvents;
} // namespace

TEST(EntityEventsTest, InvokeWithoutSubscriberDoesNothing)
{
    EntityEvent ev;

    ev.Invoke();

    EXPECT_EQ(ev.SubscriberCount(), std::size_t{0});
}

TEST(EntityEventsTest, SingleSubscriberIsCalledOnce)
{
    EntityEvent ev;
    int count = 0;
    ev.Subscribe([&count]() { ++count; });

    ev.Invoke();

    EXPECT_EQ(count, 1);
}

TEST(EntityEventsTest, TwoSubscribersAreBothCalled)
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

TEST(EntityEventsTest, SubscriberCountMatchesTheNumberAdded)
{
    EntityEvent ev;

    EXPECT_EQ(ev.SubscriberCount(), std::size_t{0});
    ev.Subscribe([]() {});
    EXPECT_EQ(ev.SubscriberCount(), std::size_t{1});
    ev.Subscribe([]() {});
    EXPECT_EQ(ev.SubscriberCount(), std::size_t{2});
}

TEST(EntityEventsTest, EmptyCallbackIsDropped)
{
    EntityEvent ev;

    ev.Subscribe(std::function<void()>{});
    ev.Invoke();

    EXPECT_EQ(ev.SubscriberCount(), std::size_t{0});
}

TEST(EntityEventsTest, GroundEnterAndGroundExitAreSeparate)
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

TEST(EntityEventsTest, PlayerNotificationsAreSeparate)
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
