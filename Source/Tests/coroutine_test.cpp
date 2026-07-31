#include "Runtime/Core/Coroutine.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

/// 台本実行器の単体検証。待ちの明け方・破棄・複数本の独立を dt 駆動で確かめる

namespace
{
    NS::Core::Coroutine WaitSecondsScript(std::vector<std::string>& log, float seconds)
    {
        log.push_back("before");
        co_await NS::Core::WaitSeconds{seconds};
        log.push_back("after");
    }

    NS::Core::Coroutine WaitGateScript(std::vector<std::string>& log, const bool& gate)
    {
        log.push_back("before");
        co_await NS::Core::WaitUntil{[&gate] { return gate; }};
        log.push_back("after");
    }

    NS::Core::Coroutine NextFrameScript(std::vector<std::string>& log)
    {
        log.push_back("before");
        co_await NS::Core::NextFrame{};
        log.push_back("after");
    }

    NS::Core::Coroutine SynchronousScript(std::vector<std::string>& log)
    {
        log.push_back("only");
        co_return;
    }

    // 台本フレームの破棄で立つ旗。CancelAll が中の破棄処理を走らせる証拠に使う
    struct DtorFlag
    {
        bool* flag = nullptr;
        ~DtorFlag()
        {
            if (flag != nullptr)
                *flag = true;
        }
    };

    NS::Core::Coroutine HoldRaiiScript(std::vector<std::string>& log, bool& destroyed)
    {
        DtorFlag raii{&destroyed};
        log.push_back("before");
        co_await NS::Core::WaitUntil{[] { return false; }};
        log.push_back("after");
    }
} // namespace

TEST(CoroutineTest, StartRunsBodyUntilFirstWait)
{
    std::vector<std::string> log;
    NS::Core::CoroutineRunner runner;

    runner.Start(WaitSecondsScript(log, 1.0f));

    EXPECT_EQ(log, (std::vector<std::string>{"before"}));
    EXPECT_TRUE(runner.IsRunning());
}

TEST(CoroutineTest, WaitSecondsResumesAfterAccumulatedTime)
{
    std::vector<std::string> log;
    NS::Core::CoroutineRunner runner;
    runner.Start(WaitSecondsScript(log, 0.05f));

    runner.Tick(0.02f);
    runner.Tick(0.02f);
    EXPECT_TRUE(runner.IsRunning());
    EXPECT_EQ(log.size(), 1u);

    runner.Tick(0.02f);
    EXPECT_FALSE(runner.IsRunning());
    EXPECT_EQ(log, (std::vector<std::string>{"before", "after"}));
}

TEST(CoroutineTest, WaitUntilResumesWhenConditionTurnsTrue)
{
    std::vector<std::string> log;
    bool gate = false;
    NS::Core::CoroutineRunner runner;
    runner.Start(WaitGateScript(log, gate));

    runner.Tick(1.0f);
    runner.Tick(1.0f);
    EXPECT_TRUE(runner.IsRunning());

    gate = true;
    runner.Tick(1.0f);
    EXPECT_FALSE(runner.IsRunning());
    EXPECT_EQ(log, (std::vector<std::string>{"before", "after"}));
}

TEST(CoroutineTest, WaitUntilAlreadyTrueContinuesWithoutSuspending)
{
    std::vector<std::string> log;
    const bool gate = true;
    NS::Core::CoroutineRunner runner;

    // 最初から真の条件は待たず、台本は Start の中で最後まで走り切る
    runner.Start(WaitGateScript(log, gate));

    EXPECT_FALSE(runner.IsRunning());
    EXPECT_EQ(log, (std::vector<std::string>{"before", "after"}));
}

TEST(CoroutineTest, NextFrameResumesOnFollowingTick)
{
    std::vector<std::string> log;
    NS::Core::CoroutineRunner runner;
    runner.Start(NextFrameScript(log));
    EXPECT_TRUE(runner.IsRunning());

    runner.Tick(0.0f);
    EXPECT_FALSE(runner.IsRunning());
    EXPECT_EQ(log, (std::vector<std::string>{"before", "after"}));
}

TEST(CoroutineTest, SynchronousScriptFinishesAtStart)
{
    std::vector<std::string> log;
    NS::Core::CoroutineRunner runner;
    runner.Start(SynchronousScript(log));

    EXPECT_FALSE(runner.IsRunning());
    EXPECT_EQ(log, (std::vector<std::string>{"only"}));
}

TEST(CoroutineTest, CancelAllDestroysFramesWithoutResuming)
{
    std::vector<std::string> log;
    bool destroyed = false;
    NS::Core::CoroutineRunner runner;
    runner.Start(HoldRaiiScript(log, destroyed));
    runner.Tick(1.0f);
    ASSERT_TRUE(runner.IsRunning());

    runner.CancelAll();

    // 続きは走らないが、台本フレーム内の破棄処理は走る
    EXPECT_FALSE(runner.IsRunning());
    EXPECT_TRUE(destroyed);
    EXPECT_EQ(log, (std::vector<std::string>{"before"}));
}

TEST(CoroutineTest, MultipleScriptsRunIndependently)
{
    std::vector<std::string> logA;
    std::vector<std::string> logB;
    NS::Core::CoroutineRunner runner;
    runner.Start(WaitSecondsScript(logA, 0.01f));
    runner.Start(WaitSecondsScript(logB, 0.03f));

    runner.Tick(0.02f);
    EXPECT_EQ(logA.size(), 2u);
    EXPECT_EQ(logB.size(), 1u);
    EXPECT_TRUE(runner.IsRunning());

    runner.Tick(0.02f);
    EXPECT_EQ(logB.size(), 2u);
    EXPECT_FALSE(runner.IsRunning());
}
