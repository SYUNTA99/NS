#include "Game/Level/ImpactOutcome.h"

#include <gtest/gtest.h>

namespace
{
    using NS::Game::Level::ComputeImpactOutcome;
    using NS::Game::Level::ImpactInput;
    using NS::Game::Level::ImpactOutcome;
    using NS::Game::Level::ImpactTuning;

    // 質量 1・威力 1・中心から外れた当たり。軸に沿って当てた形
    ImpactInput BaseInput()
    {
        ImpactInput input;
        input.power = 1.0f;
        input.centerHit = false;
        input.mass = 1.0f;
        input.awayDirection = NS::Core::Vector3{1.0f, 0.0f, 0.0f};
        input.launchDirection = NS::Core::Vector3{0.0f, 0.0f, 1.0f};
        input.slamVelocity = NS::Core::Vector3{-8.0f, 0.0f, 0.0f};
        return input;
    }
} // namespace

// ピラー 2: 重い相手ほど自分が大きく弾かれ、相手は少ししか飛ばない
TEST(ImpactOutcome, HeavierTargetBouncesSelfMoreAndLaunchesTargetLess)
{
    const ImpactTuning tuning{};
    ImpactInput light = BaseInput();
    light.mass = 0.5f;
    ImpactInput heavy = BaseInput();
    heavy.mass = 4.0f;

    const ImpactOutcome lightOutcome = ComputeImpactOutcome(light, tuning);
    const ImpactOutcome heavyOutcome = ComputeImpactOutcome(heavy, tuning);

    EXPECT_GT(heavyOutcome.reboundArc.apexHeight, lightOutcome.reboundArc.apexHeight);
    EXPECT_GT(heavyOutcome.reboundArc.distance, lightOutcome.reboundArc.distance);
    EXPECT_LT(heavyOutcome.launchArc.distance, lightOutcome.launchArc.distance);
    EXPECT_LT(heavyOutcome.launchArc.apexHeight, lightOutcome.launchArc.apexHeight);
    EXPECT_FALSE(heavyOutcome.broke);
}

// 中心近くの当たりだけ反動の距離を伸ばし、高さと向きは変えない。止めも長くなる
TEST(ImpactOutcome, CenterHitStretchesReboundDistanceAndHitStopButNotHeight)
{
    const ImpactTuning tuning{};
    ImpactInput wide = BaseInput();
    ImpactInput center = BaseInput();
    center.centerHit = true;

    const ImpactOutcome wideOutcome = ComputeImpactOutcome(wide, tuning);
    const ImpactOutcome centerOutcome = ComputeImpactOutcome(center, tuning);

    EXPECT_FLOAT_EQ(centerOutcome.reboundArc.distance,
                    wideOutcome.reboundArc.distance * tuning.centerHitReboundDistanceScale);
    EXPECT_FLOAT_EQ(centerOutcome.reboundArc.apexHeight, wideOutcome.reboundArc.apexHeight);
    EXPECT_GT(centerOutcome.stopSteps, wideOutcome.stopSteps);
}

// 威力が 0 の当たりは止めず、動かさない。質量が極端に重くても止めは上限で頭打ち
TEST(ImpactOutcome, HitStopStepsAreZeroWithoutPowerAndCappedByMax)
{
    const ImpactTuning tuning{};
    ImpactInput weak = BaseInput();
    weak.power = 0.0f;
    ImpactInput huge = BaseInput();
    huge.mass = 1000000.0f;

    const ImpactOutcome weakOutcome = ComputeImpactOutcome(weak, tuning);
    const ImpactOutcome hugeOutcome = ComputeImpactOutcome(huge, tuning);

    EXPECT_EQ(weakOutcome.stopSteps, 0);
    EXPECT_FLOAT_EQ(weakOutcome.reboundScale, 0.0f);
    EXPECT_FLOAT_EQ(weakOutcome.launchScale, 0.0f);
    EXPECT_EQ(hugeOutcome.stopSteps, 12);
}

// 破壊を許し、壊れる相手で威力が耐久に届くと貫通する。貫通は相手を飛ばさず自機も反動しない
TEST(ImpactOutcome, BreakNeedsEnabledBreakableAndPowerReachingToughness)
{
    ImpactTuning tuning{};
    tuning.breakEnabled = true;
    ImpactInput input = BaseInput();
    input.breakable = true;
    input.toughness = 1.0f;

    const ImpactOutcome broken = ComputeImpactOutcome(input, tuning);
    EXPECT_TRUE(broken.broke);
    EXPECT_FLOAT_EQ(broken.launchArc.distance, 0.0f);
    EXPECT_FLOAT_EQ(broken.reboundArc.distance, 0.0f);
    EXPECT_FLOAT_EQ(broken.launchScale, 0.0f);
    EXPECT_FLOAT_EQ(broken.breakSelfVelocity.x, input.slamVelocity.x * tuning.breakSpeedScale);

    input.toughness = 1.5f;
    EXPECT_FALSE(ComputeImpactOutcome(input, tuning).broke);

    input.toughness = 1.0f;
    input.breakable = false;
    EXPECT_FALSE(ComputeImpactOutcome(input, tuning).broke);

    input.breakable = true;
    tuning.breakEnabled = false;
    EXPECT_FALSE(ComputeImpactOutcome(input, tuning).broke);
}

// 質量 1・威力 1 の既定値を固める。調整値の既定を変えたら一緒に直す
TEST(ImpactOutcome, DefaultTuningAtMassOneAndPowerOne)
{
    const ImpactTuning tuning{};
    const ImpactOutcome outcome = ComputeImpactOutcome(BaseInput(), tuning);

    EXPECT_FALSE(outcome.broke);
    EXPECT_FLOAT_EQ(outcome.massFactor, 0.5f);
    EXPECT_FLOAT_EQ(outcome.reboundScale, 1.0f);
    EXPECT_FLOAT_EQ(outcome.launchScale, 1.0f);
    EXPECT_FLOAT_EQ(outcome.reboundArc.apexHeight, 1.15f);
    EXPECT_FLOAT_EQ(outcome.reboundArc.distance, 0.575f);
    EXPECT_FLOAT_EQ(outcome.reboundArc.direction.x, 1.0f);
    EXPECT_FLOAT_EQ(outcome.launchArc.distance, 29.0f);
    EXPECT_FLOAT_EQ(outcome.launchArc.apexHeight, 2.0f);
    EXPECT_FLOAT_EQ(outcome.launchArc.direction.z, 1.0f);
    EXPECT_FLOAT_EQ(outcome.shakeAmplitude, 0.025f);
    EXPECT_EQ(outcome.stopSteps, 4);
}
