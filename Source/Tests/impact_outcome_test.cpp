#include "Game/Level/HitTier.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Player/PlayerParams.h"

#include <gtest/gtest.h>

namespace
{
    using NS::Game::Level::ComputeImpactOutcome;
    using NS::Game::Level::HitTier;
    using NS::Game::Level::ImpactInput;
    using NS::Game::Level::ImpactOutcome;
    using NS::Game::Level::ImpactTuning;
    using NS::Game::Level::MakeImpactTuning;

    // 既定の PlayerParams から作った調整値。値の正は PlayerParams 1 つなので、ここでも同じ作り方を通す
    ImpactTuning DefaultTuning()
    {
        const NS::Game::Player::PlayerParams params;
        return MakeImpactTuning(params);
    }

    // 質量 1・威力 1・中心から外れた当たり。軸に沿って当てた形
    ImpactInput BaseInput()
    {
        ImpactInput input;
        input.power = 1.0f;
        input.tier = HitTier::Wide;
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
    const ImpactTuning tuning = DefaultTuning();
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

// 中心近くの当たりだけ反動の距離を伸ばす。外れは高さを欄「外れの反動の高さの割合」で下げ、真ん中より浮かせない
TEST(ImpactOutcome, CenterHitStretchesReboundDistanceAndMissStaysLow)
{
    const ImpactTuning tuning = DefaultTuning();
    ImpactInput wide = BaseInput();
    ImpactInput center = BaseInput();
    center.tier = HitTier::Center;

    const ImpactOutcome wideOutcome = ComputeImpactOutcome(wide, tuning);
    const ImpactOutcome centerOutcome = ComputeImpactOutcome(center, tuning);

    EXPECT_FLOAT_EQ(centerOutcome.reboundArc.distance,
                    wideOutcome.reboundArc.distance * tuning.centerHitReboundDistanceScale);
    EXPECT_FLOAT_EQ(wideOutcome.reboundArc.apexHeight,
                    centerOutcome.reboundArc.apexHeight * tuning.missReboundHeightRatio);
    EXPECT_LT(wideOutcome.reboundArc.apexHeight, centerOutcome.reboundArc.apexHeight);
}

// 段が押し飛ばしの量に効くのは中心近くの反動の距離の倍率と外れの高さの割合だけ。相手の飛ぶ量は段で変えない
TEST(ImpactOutcome, TiersDifferOnlyByTheCenterDistanceAndMissHeight)
{
    ImpactTuning tuning = DefaultTuning();
    tuning.centerHitReboundDistanceScale = 1.0f;
    tuning.missReboundHeightRatio = 1.0f;
    ImpactInput wide = BaseInput();
    ImpactInput center = BaseInput();
    center.tier = HitTier::Center;

    const ImpactOutcome wideOutcome = ComputeImpactOutcome(wide, tuning);
    const ImpactOutcome centerOutcome = ComputeImpactOutcome(center, tuning);

    EXPECT_FLOAT_EQ(centerOutcome.reboundArc.distance, wideOutcome.reboundArc.distance);
    EXPECT_FLOAT_EQ(centerOutcome.reboundArc.apexHeight, wideOutcome.reboundArc.apexHeight);
    EXPECT_FLOAT_EQ(centerOutcome.launchArc.distance, wideOutcome.launchArc.distance);
}

// 押し飛ばしの当たりの止めの長さはタイムラインが持ち、配分は決めない。威力が 0 の当たりは動かさない
TEST(ImpactOutcome, PushHitsLeaveTheStopToTheTimeline)
{
    const ImpactTuning tuning = DefaultTuning();
    ImpactInput weak = BaseInput();
    weak.power = 0.0f;
    ImpactInput huge = BaseInput();
    huge.mass = 1000000.0f;

    const ImpactOutcome weakOutcome = ComputeImpactOutcome(weak, tuning);
    const ImpactOutcome hugeOutcome = ComputeImpactOutcome(huge, tuning);

    EXPECT_EQ(weakOutcome.stopSteps, 0);
    EXPECT_FLOAT_EQ(weakOutcome.reboundScale, 0.0f);
    EXPECT_FLOAT_EQ(weakOutcome.launchScale, 0.0f);
    EXPECT_EQ(hugeOutcome.stopSteps, 0);
}

// 貫通の止めだけは欄「貫通の止め秒」のまま。中心近くは段の止めの倍率を掛け、上限で頭打ち
TEST(ImpactOutcome, BreakStopIsTheBreakSecondsScaledByTheTier)
{
    ImpactTuning tuning = DefaultTuning();
    tuning.breakEnabled = true;
    ImpactInput wide = BaseInput();
    wide.breakable = true;
    wide.toughness = 1.0f;
    ImpactInput center = wide;
    center.tier = HitTier::Center;

    EXPECT_EQ(ComputeImpactOutcome(wide, tuning).stopSteps, 4);
    EXPECT_EQ(ComputeImpactOutcome(center, tuning).stopSteps, 8);
    tuning.breakStopSeconds = 1.0f;
    EXPECT_EQ(ComputeImpactOutcome(wide, tuning).stopSteps, 12);
}

// 破壊を許し、壊れる相手で威力が耐久に届くと貫通する。貫通は相手を飛ばさず自機も反動しない
TEST(ImpactOutcome, BreakNeedsEnabledBreakableAndPowerReachingToughness)
{
    ImpactTuning tuning = DefaultTuning();
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

// 既定の PlayerParams から作った調整値で、質量 1・威力 1 の結果を固める。PlayerParams の既定を変えたら一緒に直す
TEST(ImpactOutcome, DefaultTuningAtMassOneAndPowerOne)
{
    const ImpactTuning tuning = DefaultTuning();
    const ImpactOutcome outcome = ComputeImpactOutcome(BaseInput(), tuning);

    EXPECT_FALSE(outcome.broke);
    EXPECT_FLOAT_EQ(outcome.massFactor, 0.5f);
    EXPECT_FLOAT_EQ(outcome.reboundScale, 1.0f);
    EXPECT_FLOAT_EQ(outcome.launchScale, 1.0f);
    EXPECT_FLOAT_EQ(outcome.reboundArc.apexHeight, 1.15f * 0.3f);
    EXPECT_FLOAT_EQ(outcome.reboundArc.distance, 0.575f);
    EXPECT_FLOAT_EQ(outcome.reboundArc.direction.x, 1.0f);
    EXPECT_FLOAT_EQ(outcome.launchArc.distance, 29.0f);
    EXPECT_FLOAT_EQ(outcome.launchArc.apexHeight, 2.0f);
    // 面の真ん中の外れは、押し込む向き (突進の向き) へ飛ばす
    EXPECT_FLOAT_EQ(outcome.launchArc.direction.x, -1.0f);
    EXPECT_EQ(outcome.stopSteps, 0);

    const ImpactOutcome center = ComputeImpactOutcome(
        [] {
            ImpactInput input = BaseInput();
            input.tier = HitTier::Center;
            return input;
        }(),
        tuning);
    EXPECT_FLOAT_EQ(center.reboundArc.apexHeight, 1.15f);
    EXPECT_FLOAT_EQ(center.launchArc.direction.z, 1.0f);
}

namespace
{
    // +Z へ進む突進の外れ。面の右は +X
    ImpactInput MissAlongZ(float u, float v, NS::Obj::HitSensorShape shape)
    {
        ImpactInput input = BaseInput();
        input.slamVelocity = NS::Core::Vector3{0.0f, 0.0f, 8.0f};
        input.awayDirection = NS::Core::Vector3{0.0f, 0.0f, -1.0f};
        input.launchDirection = NS::Core::Vector3{0.0f, 0.0f, 1.0f};
        input.faceU = u;
        input.faceV = v;
        input.bodyShape = shape;
        return input;
    }
} // namespace

// 外れの面の向きは面の上の位置から出す。丸は球の表面、四角は角を少し丸めた箱の表面 (角の鋭さ 6) として読む
TEST(ImpactOutcome, MissSurfaceNormalReadsTheFacePositionByShape)
{
    const NS::Core::Vector3 forward{0.0f, 0.0f, 1.0f};
    const NS::Core::Vector3 middle = NS::Game::Level::MissSurfaceNormal(0.0f, 0.0f, 2.0f, forward);
    EXPECT_NEAR(middle.z, -1.0f, 0.0001f);
    // 右寄り・少し上の当たり。丸は上へ 0.40、四角は縁に沿って右へ逸れ、上へは 0.02
    const NS::Core::Vector3 round = NS::Game::Level::MissSurfaceNormal(0.85f, 0.4f, 2.0f, forward);
    const NS::Core::Vector3 box = NS::Game::Level::MissSurfaceNormal(0.85f, 0.4f, 6.0f, forward);
    EXPECT_NEAR(round.y, 0.40f, 0.005f);
    EXPECT_NEAR(box.y, 0.02f, 0.005f);
    EXPECT_GT(round.x, 0.0f);
    EXPECT_NEAR(round.Length(), 1.0f, 0.0001f);
    EXPECT_NEAR(box.Length(), 1.0f, 0.0001f);
    // 縁の外 (1 を超える位置) は縁で頭打ち
    const NS::Core::Vector3 beyond = NS::Game::Level::MissSurfaceNormal(1.4f, 0.0f, 2.0f, forward);
    EXPECT_NEAR(beyond.x, 1.0f, 0.0001f);
}

// 外れは面に沿って滑る勢いが横へ逃げ、相手の脇を逸れる。右寄りなら右、左寄りなら左。真ん中の当たりは並びで弾く
TEST(ImpactOutcome, MissDeflectsTowardTheSideItSlidesOff)
{
    const ImpactTuning tuning = DefaultTuning();
    const ImpactOutcome right = ComputeImpactOutcome(MissAlongZ(0.8f, 0.0f, NS::Obj::HitSensorShape::Sphere), tuning);
    // 面の向き n = 0.8 右 + 0.6 手前。来た向きを n で鏡に映すと 0.96 右 + 0.28 前
    EXPECT_NEAR(right.reboundArc.direction.x, 0.96f, 0.001f);
    EXPECT_NEAR(right.reboundArc.direction.z, 0.28f, 0.001f);
    // 相手は押し込む向き (−n) だけへ押される
    EXPECT_NEAR(right.launchArc.direction.x, -0.8f, 0.001f);
    EXPECT_NEAR(right.launchArc.direction.z, 0.6f, 0.001f);

    const ImpactOutcome left = ComputeImpactOutcome(MissAlongZ(-0.8f, 0.0f, NS::Obj::HitSensorShape::Sphere), tuning);
    EXPECT_NEAR(left.reboundArc.direction.x, -0.96f, 0.001f);

    ImpactInput center = MissAlongZ(0.8f, 0.0f, NS::Obj::HitSensorShape::Sphere);
    center.tier = HitTier::Center;
    const ImpactOutcome centerOutcome = ComputeImpactOutcome(center, tuning);
    EXPECT_FLOAT_EQ(centerOutcome.reboundArc.direction.z, -1.0f);
    EXPECT_FLOAT_EQ(centerOutcome.launchArc.direction.z, 1.0f);
}

// 下の縁の外れは地面へ叩きつけられ、外れの高さよりさらに低く跳ねる。上の縁は外れの高さより浮かない
TEST(ImpactOutcome, MissOffTheBottomBouncesLowerAndTheTopDoesNotFloat)
{
    const ImpactTuning tuning = DefaultTuning();
    const float plain = ComputeImpactOutcome(MissAlongZ(0.0f, 0.0f, NS::Obj::HitSensorShape::Sphere), tuning)
                            .reboundArc.apexHeight;
    const float bottom = ComputeImpactOutcome(MissAlongZ(0.0f, -0.8f, NS::Obj::HitSensorShape::Sphere), tuning)
                             .reboundArc.apexHeight;
    const float top =
        ComputeImpactOutcome(MissAlongZ(0.0f, 0.8f, NS::Obj::HitSensorShape::Sphere), tuning).reboundArc.apexHeight;
    // 真下を向く分 0.8 だけ、跳ねの割合 0.4 へ寄せる
    EXPECT_NEAR(bottom, plain * (1.0f - (1.0f - tuning.missSlamBounce) * 0.8f), 0.0001f);
    EXPECT_FLOAT_EQ(top, plain);
}
