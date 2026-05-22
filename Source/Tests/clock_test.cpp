#include <gtest/gtest.h>

#include <Framework/Core/Clock.h>
#include <Framework/Core/Logger.h>

#include <chrono>
#include <thread>

class ClockLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST(NsCoreClock, NowIsMonotonic)
{
    const auto t1 = NS::Core::Clock::Now();
    const auto t2 = NS::Core::Clock::Now();
    EXPECT_GE(t2, t1);
}

TEST(NsCoreClock, ElapsedSecondsIsMonotonic)
{
    const double t1 = NS::Core::Clock::ElapsedSeconds();
    const double t2 = NS::Core::Clock::ElapsedSeconds();
    EXPECT_GE(t2, t1);
    EXPECT_GE(t1, 0.0);
}

TEST(NsCoreFrameTimer, TickAdvancesState)
{
    NS::Core::FrameTimer ft;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ft.Tick();
    EXPECT_GT(ft.DeltaSeconds(), 0.0f);
    EXPECT_EQ(ft.FrameNumber(), 1ULL);
}

TEST(NsCoreFrameTimer, ResetClearsState)
{
    NS::Core::FrameTimer ft;
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
    NS::Core::FrameTimer ft;
    ft.SetFixedDelta(1.0f / 60.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ft.Tick();
    EXPECT_GE(ft.FixedStepsThisFrame(), 1);
}

TEST_F(ClockLoggerTest, ScopedTimerLogsOnDestruction)
{
    {
        NS_SCOPED_TIMER(NS::Core::LogCat::Core, "test_label");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    SUCCEED();
}
