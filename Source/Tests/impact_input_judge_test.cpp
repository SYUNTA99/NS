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

// 押した歩で出すとチャージ狙いにもタップが混ざる。発動点は離した歩だけに絞る
TEST(ImpactInputJudge, PressAloneFiresNothing)
{
    ImpactInputJudge judge;
    judge.Step(true);

    EXPECT_EQ(judge.TakeFired(), SlamKind::None);
}

TEST(ImpactInputJudge, JustPressedMarksOnlyTheFirstHeldStep)
{
    ImpactInputJudge judge;
    EXPECT_FALSE(judge.JustPressed());
    EXPECT_FALSE(judge.IsHeld());

    judge.Step(true);
    EXPECT_TRUE(judge.JustPressed());
    EXPECT_TRUE(judge.IsHeld());

    judge.Step(true);
    EXPECT_FALSE(judge.JustPressed());
    EXPECT_TRUE(judge.IsHeld());

    judge.Step(false);
    EXPECT_FALSE(judge.JustPressed());
    EXPECT_FALSE(judge.IsHeld());

    judge.Step(true);
    EXPECT_TRUE(judge.JustPressed());
}

TEST(ImpactInputJudge, ReleaseBeforeThresholdFiresTap)
{
    ImpactInputJudge judge;
    StepHeld(judge, judge.chargeThresholdSteps - 1);
    ASSERT_EQ(judge.TakeFired(), SlamKind::None);
    EXPECT_FALSE(judge.IsCharging());

    judge.Step(false);

    EXPECT_EQ(judge.TakeFired(), SlamKind::Tap);
    EXPECT_FLOAT_EQ(judge.Charge01(), 0.0f);
}

// 境目を素側に置くと溜めたのにチャージが出ないので、しきい値ちょうどをチャージ側で固定する
TEST(ImpactInputJudge, ThresholdStepReleasesAsCharged)
{
    ImpactInputJudge judge;
    StepHeld(judge, judge.chargeThresholdSteps);
    EXPECT_TRUE(judge.IsCharging());

    judge.Step(false);

    EXPECT_EQ(judge.TakeFired(), SlamKind::Charged);
}

TEST(ImpactInputJudge, HoldingFiresNothingUntilRelease)
{
    ImpactInputJudge judge;
    for (int i = 0; i < 100; ++i)
    {
        judge.Step(true);
        ASSERT_EQ(judge.TakeFired(), SlamKind::None);
    }
    EXPECT_TRUE(judge.IsCharging());
    EXPECT_TRUE(judge.IsChargeFull());
}

// タップとチャージの排他そのもの。溜めてから離すまでの間にタップが 1 回も混ざってはいけない
TEST(ImpactInputJudge, ChargedRunNeverFiresTap)
{
    ImpactInputJudge judge;
    for (int i = 0; i < judge.chargeMaxSteps; ++i)
    {
        judge.Step(true);
        ASSERT_NE(judge.TakeFired(), SlamKind::Tap);
    }

    judge.Step(false);

    EXPECT_EQ(judge.TakeFired(), SlamKind::Charged);
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
    judge.Step(false);
    EXPECT_EQ(judge.TakeFired(), SlamKind::Tap);
    EXPECT_EQ(judge.TakeFired(), SlamKind::None);

    StepHeld(judge, judge.chargeMaxSteps);
    judge.Step(false);

    EXPECT_EQ(judge.TakeFired(), SlamKind::Charged);
    EXPECT_EQ(judge.TakeFired(), SlamKind::None);
    StepIdle(judge, 5);
    EXPECT_EQ(judge.TakeFired(), SlamKind::None);
}

TEST(ImpactInputJudge, RepressRestartsCharge)
{
    ImpactInputJudge judge;
    StepHeld(judge, judge.chargeMaxSteps);
    judge.Step(false);
    ASSERT_EQ(judge.TakeFired(), SlamKind::Charged);

    StepHeld(judge, 2);
    EXPECT_FALSE(judge.IsCharging());
    EXPECT_FLOAT_EQ(judge.Charge01(), 0.0f);

    judge.Step(false);
    EXPECT_EQ(judge.TakeFired(), SlamKind::Tap);
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

// 溜めの入り口で速度を落とすため、入った歩を 1 回だけ知らせる
TEST(ImpactInputJudge, JustStartedChargingIsTrueOnlyOnTheEntryStep)
{
    ImpactInputJudge judge;
    judge.chargeThresholdSteps = 3;
    judge.chargeMaxSteps = 10;

    judge.Step(true);
    EXPECT_FALSE(judge.JustStartedCharging());
    judge.Step(true);
    EXPECT_FALSE(judge.JustStartedCharging());
    judge.Step(true);
    EXPECT_TRUE(judge.JustStartedCharging());
    judge.Step(true);
    EXPECT_FALSE(judge.JustStartedCharging());

    // 押し直すたびに入り口はもう一度来る
    judge.Step(false);
    StepHeld(judge, 3);
    EXPECT_TRUE(judge.JustStartedCharging());
}

// しきい値を 0 以下にされると押した歩からチャージ扱いになる。入り口もその歩へ揃える
TEST(ImpactInputJudge, NonPositiveThresholdStartsChargingOnTheFirstStep)
{
    ImpactInputJudge judge;
    judge.chargeThresholdSteps = 0;
    judge.chargeMaxSteps = 10;

    EXPECT_FALSE(judge.JustStartedCharging());
    judge.Step(true);
    EXPECT_TRUE(judge.JustStartedCharging());
    judge.Step(true);
    EXPECT_FALSE(judge.JustStartedCharging());
}
