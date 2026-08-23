#include <Game/Level/ImpactInputJudge.h>
#include <gtest/gtest.h>

namespace
{
    using NS::Game::Level::ImpactInputJudge;
    using NS::Game::Level::SlamKind;

    void StepIdle(ImpactInputJudge& judge, int steps)
    {
        for (int i = 0; i < steps; ++i)
            judge.Step(false);
    }

    void StepHeld(ImpactInputJudge& judge, int steps)
    {
        for (int i = 0; i < steps; ++i)
            judge.Step(true);
    }
} // namespace

TEST(ImpactInputJudge, IdleNeverFires)
{
    ImpactInputJudge judge;
    StepIdle(judge, 10);

    EXPECT_EQ(judge.TakeFired(), SlamKind::None);
    EXPECT_FALSE(judge.IsCharging());
    EXPECT_FALSE(judge.IsChargeFull());
    EXPECT_FLOAT_EQ(judge.Charge01(), 0.0f);
}

// 押した瞬間に技が出る手応えが要件。離し待ちだと押下の歩に何も起きない
TEST(ImpactInputJudge, PressFiresTapImmediately)
{
    ImpactInputJudge judge;
    judge.Step(true);

    EXPECT_EQ(judge.TakeFired(), SlamKind::Tap);
}

// タップは押した歩で出済み。しきい値前の離しが二発目を出してはいけない
TEST(ImpactInputJudge, ReleaseBeforeThresholdAddsNothing)
{
    ImpactInputJudge judge;
    judge.Step(true);
    ASSERT_EQ(judge.TakeFired(), SlamKind::Tap);
    StepHeld(judge, judge.chargeThresholdSteps - 2);
    EXPECT_FALSE(judge.IsCharging());

    judge.Step(false);

    EXPECT_EQ(judge.TakeFired(), SlamKind::None);
    EXPECT_FLOAT_EQ(judge.Charge01(), 0.0f);
}

// 境目を素側に置くと溜めたのにチャージが出ないので、しきい値ちょうどをチャージ側で固定する
TEST(ImpactInputJudge, ThresholdStepReleasesAsCharged)
{
    ImpactInputJudge judge;
    judge.Step(true);
    ASSERT_EQ(judge.TakeFired(), SlamKind::Tap);
    StepHeld(judge, judge.chargeThresholdSteps - 1);
    EXPECT_TRUE(judge.IsCharging());

    judge.Step(false);

    EXPECT_EQ(judge.TakeFired(), SlamKind::Charged);
}

TEST(ImpactInputJudge, HoldingFiresOnlyThePressTap)
{
    ImpactInputJudge judge;
    judge.Step(true);
    ASSERT_EQ(judge.TakeFired(), SlamKind::Tap);
    for (int i = 0; i < 99; ++i)
    {
        judge.Step(true);
        ASSERT_EQ(judge.TakeFired(), SlamKind::None);
    }
    EXPECT_TRUE(judge.IsCharging());
    EXPECT_TRUE(judge.IsChargeFull());
}

TEST(ImpactInputJudge, FullChargeClampsAtOne)
{
    ImpactInputJudge judge;
    StepHeld(judge, judge.chargeMaxSteps + 60);

    EXPECT_TRUE(judge.IsChargeFull());
    EXPECT_FLOAT_EQ(judge.Charge01(), 1.0f);
}

TEST(ImpactInputJudge, ChargeRampsBetweenThresholdAndFull)
{
    ImpactInputJudge judge;
    const int half = judge.chargeThresholdSteps + (judge.chargeMaxSteps - judge.chargeThresholdSteps) / 2;
    StepHeld(judge, half);

    EXPECT_FALSE(judge.IsChargeFull());
    EXPECT_NEAR(judge.Charge01(), 0.5f, 0.02f);
}

// 離した歩で 0 に戻すと発動要求へ渡す溜め量が消えるため、その歩は読める
TEST(ImpactInputJudge, ChargeIsReadableOnReleaseStep)
{
    ImpactInputJudge judge;
    StepHeld(judge, judge.chargeMaxSteps);

    judge.Step(false);

    EXPECT_EQ(judge.TakeFired(), SlamKind::Charged);
    EXPECT_FLOAT_EQ(judge.Charge01(), 1.0f);
    EXPECT_FALSE(judge.IsCharging());
}

TEST(ImpactInputJudge, FiredIsConsumedOnce)
{
    ImpactInputJudge judge;
    judge.Step(true);
    EXPECT_EQ(judge.TakeFired(), SlamKind::Tap);
    EXPECT_EQ(judge.TakeFired(), SlamKind::None);

    StepHeld(judge, judge.chargeMaxSteps);
    judge.Step(false);

    EXPECT_EQ(judge.TakeFired(), SlamKind::Charged);
    EXPECT_EQ(judge.TakeFired(), SlamKind::None);
    StepIdle(judge, 5);
    EXPECT_EQ(judge.TakeFired(), SlamKind::None);
}

TEST(ImpactInputJudge, RepressFiresTapAndRestartsCharge)
{
    ImpactInputJudge judge;
    StepHeld(judge, judge.chargeMaxSteps);
    judge.Step(false);
    ASSERT_EQ(judge.TakeFired(), SlamKind::Charged);

    judge.Step(true);
    EXPECT_EQ(judge.TakeFired(), SlamKind::Tap);
    StepHeld(judge, 2);
    EXPECT_FALSE(judge.IsCharging());
    EXPECT_FLOAT_EQ(judge.Charge01(), 0.0f);

    judge.Step(false);
    EXPECT_EQ(judge.TakeFired(), SlamKind::None);
}

// 欄は Inspector から 0 以下にできるため、無入力のチャージ扱いと 0 除算を見張る
TEST(ImpactInputJudge, NonPositiveFieldsStaySafe)
{
    ImpactInputJudge judge;
    judge.chargeThresholdSteps = 0;
    judge.chargeMaxSteps = 0;

    StepIdle(judge, 5);
    EXPECT_FALSE(judge.IsCharging());
    EXPECT_FLOAT_EQ(judge.Charge01(), 0.0f);

    judge.Step(true);
    EXPECT_TRUE(judge.IsCharging());
    EXPECT_TRUE(judge.IsChargeFull());
    EXPECT_FLOAT_EQ(judge.Charge01(), 1.0f);
}
