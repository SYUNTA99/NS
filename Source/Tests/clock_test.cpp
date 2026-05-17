#include <gtest/gtest.h>

#include <ns/core/clock.h>
#include <ns/core/logger.h>

#include <chrono>
#include <thread>

class ClockLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { ns::core::Logger::Init(); }
    void TearDown() override { ns::core::Logger::Shutdown(); }
};

TEST(NsCoreClock, NowIsMonotonic)
{
    const auto t1 = ns::core::Clock::Now();
    const auto t2 = ns::core::Clock::Now();
    EXPECT_GE(t2, t1);
}

TEST(NsCoreClock, ElapsedSecondsIsMonotonic)
{
    const double t1 = ns::core::Clock::ElapsedSeconds();
    const double t2 = ns::core::Clock::ElapsedSeconds();
    EXPECT_GE(t2, t1);
    EXPECT_GE(t1, 0.0);
}

TEST(NsCoreFrameTimer, TickAdvancesState)
{
    ns::core::FrameTimer ft;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ft.Tick();
    EXPECT_GT(ft.DeltaSeconds(), 0.0f);
    EXPECT_EQ(ft.FrameNumber(), 1ULL);
}

TEST(NsCoreFrameTimer, ResetClearsState)
{
    ns::core::FrameTimer ft;
    for (int i = 0; i < 3; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ft.Tick();
    }
    ft.Reset();
    EXPECT_EQ(ft.FrameNumber(), 0ULL);
    EXPECT_DOUBLE_EQ(ft.TotalSeconds(), 0.0);
    EXPECT_FLOAT_EQ(ft.DeltaSeconds(), 0.0f);
}

TEST(NsCoreFrameTimer, FixedStepsAccumulate)
{
    ns::core::FrameTimer ft;
    ft.SetFixedDelta(1.0f / 60.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ft.Tick();
    EXPECT_GE(ft.FixedStepsThisFrame(), 1);
}

TEST_F(ClockLoggerTest, ScopedTimerLogsOnDestruction)
{
    {
        NS_SCOPED_TIMER(ns::core::LogCat::Core, "test_label");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    SUCCEED();
}
