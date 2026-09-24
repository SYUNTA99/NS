#include <Runtime/Platform/Clock.h>
#include <Runtime/Core/Logger.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

class ClockLoggerTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::Logger::Init(); }
    void TearDown() override { NS::Core::Logger::Shutdown(); }
};

TEST(NsCoreClock, NowIsMonotonic)
{
    const std::chrono::steady_clock::time_point t1 = NS::Platform::Clock::Now();
    const std::chrono::steady_clock::time_point t2 = NS::Platform::Clock::Now();
    EXPECT_GE(t2, t1);
}

TEST(NsCoreClock, ElapsedSecondsIsMonotonic)
{
    const double t1 = NS::Platform::Clock::ElapsedSeconds();
    const double t2 = NS::Platform::Clock::ElapsedSeconds();
    EXPECT_GE(t2, t1);
    EXPECT_GE(t1, 0.0);
}

TEST(NsCoreFrameTimer, TickAdvancesState)
{
    NS::Platform::FrameTimer::Reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    NS::Platform::FrameTimer::Tick();
    EXPECT_GT(NS::Platform::FrameTimer::DeltaSeconds(), 0.0f);
    EXPECT_EQ(NS::Platform::FrameTimer::FrameNumber(), 1ULL);
}

TEST(NsCoreFrameTimer, ResetClearsState)
{
    NS::Platform::FrameTimer::Reset();
    for (int i = 0; i < 3; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        NS::Platform::FrameTimer::Tick();
    }
    NS::Platform::FrameTimer::Reset();
    EXPECT_EQ(NS::Platform::FrameTimer::FrameNumber(), 0ULL);
    EXPECT_DOUBLE_EQ(NS::Platform::FrameTimer::TotalSeconds(), 0.0);
    EXPECT_FLOAT_EQ(NS::Platform::FrameTimer::DeltaSeconds(), 0.0f);
}

TEST(NsCoreFrameTimer, FixedStepsAccumulate)
{
    NS::Platform::FrameTimer::Reset();
    NS::Platform::FrameTimer::SetFixedDelta(1.0f / 60.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    NS::Platform::FrameTimer::Tick();
    EXPECT_GE(NS::Platform::FrameTimer::FixedStepsThisFrame(), 1);
}

TEST_F(ClockLoggerTest, ScopedTimerLogsOnDestruction)
{
    {
        NS_SCOPED_TIMER(Core, "test_label");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    SUCCEED();
}
