#include "Game/Level/HitTier.h"
#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/MapObjParams.h"
#include "Game/Player/PlayerParams.h"

#include <gtest/gtest.h>

namespace
{
    using GL::Level::ComputeImpactOutcome;
    using GL::Level::HitTier;
    using GL::Level::ImpactInput;
    using GL::Level::ImpactOutcome;
    using GL::Level::ImpactTuning;
    using GL::Level::MakeImpactTuning;

    // 既定の PlayerParams から作った調整値。本番と同じ作り方を通す
    ImpactTuning DefaultTuning()
    {
        const GL::Player::PlayerParams params;
        return MakeImpactTuning(params);
    }

    // 質量 1・威力 1・中心から外れた当たり。軸に沿って当てた形
    ImpactInput BaseInput()
    {
        ImpactInput input;
        input.power = 1.0f;
        input.tier = HitTier::Wide;
        input.mass = 1.0f;
        input.awayDirection = NS::Vector3{1.0f, 0.0f, 0.0f};
        input.launchDirection = NS::Vector3{0.0f, 0.0f, 1.0f};
        input.slamVelocity = NS::Vector3{-8.0f, 0.0f, 0.0f};
        // 飛び方は相手の欄の既定。本番は相手が当たりの答えで渡す
        input.launch = GL::Level::MapObjParams{}.Launch();
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

// 段ごとに反動の距離の倍率を掛ける。真ん中は高さを欄「中心近くの当たりの反動の高さの倍率」で下げ、低く速く後ろへ
// 戻す。外れは高さを欄「外れの反動の高さの割合」で下げ、真ん中より浮かせない
TEST(ImpactOutcome, CenterHitStretchesReboundDistanceAndMissStaysLow)
{
    const ImpactTuning tuning = DefaultTuning();
    ImpactInput wide = BaseInput();
    ImpactInput center = BaseInput();
    center.tier = HitTier::Center;

    const ImpactOutcome wideOutcome = ComputeImpactOutcome(wide, tuning);
    const ImpactOutcome centerOutcome = ComputeImpactOutcome(center, tuning);

    EXPECT_FLOAT_EQ(centerOutcome.reboundArc.distance * tuning.missReboundDistanceScale,
                    wideOutcome.reboundArc.distance * tuning.centerHitReboundDistanceScale);
    EXPECT_FLOAT_EQ(wideOutcome.reboundArc.apexHeight * tuning.centerHitReboundHeightScale,
                    centerOutcome.reboundArc.apexHeight * tuning.missReboundHeightRatio);
    // 真ん中は高さより距離を大きく伸ばす。高さに対する距離の比が、倍率を掛けない時より大きい
    EXPECT_LT(tuning.centerHitReboundHeightScale, 1.0f);
    EXPECT_GT(centerOutcome.reboundArc.distance / centerOutcome.reboundArc.apexHeight,
              tuning.reboundDistance / tuning.reboundApexHeight);
    EXPECT_LT(wideOutcome.reboundArc.apexHeight, centerOutcome.reboundArc.apexHeight);
}

// 段が押し飛ばしの量に効くのは段ごとの反動の距離と高さの倍率と外れの高さの割合だけ。相手の飛ぶ量は段で変えない
TEST(ImpactOutcome, TiersDifferOnlyByTheCenterDistanceAndMissHeight)
{
    ImpactTuning tuning = DefaultTuning();
    tuning.centerHitReboundDistanceScale = 1.0f;
    tuning.centerHitReboundHeightScale = 1.0f;
    tuning.missReboundDistanceScale = 1.0f;
    tuning.missReboundHeightRatio = 1.0f;
    ImpactInput wide = BaseInput();
    wide.launch.missDistanceRatio = 1.0f;
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
    // 外れは浮かせずに横へ滑って抜ける。高さは真ん中の 5%、距離は 2.5 倍
    EXPECT_FLOAT_EQ(outcome.reboundArc.apexHeight, 1.15f * 0.05f);
    EXPECT_FLOAT_EQ(outcome.reboundArc.distance, 0.575f * 2.5f);
    EXPECT_FLOAT_EQ(outcome.reboundArc.direction.x, 1.0f);
    // 面の真ん中の外れは押し込む成分が 1。距離は外れの距離の割合 0.1、弧は外れの高さの割合 0.35 だけ縮む
    EXPECT_FLOAT_EQ(outcome.launchArc.distance, 29.0f * 0.1f);
    EXPECT_FLOAT_EQ(outcome.launchArc.apexHeight, 2.0f * 0.35f);
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
    // 真ん中は高さを欄「中心近くの当たりの反動の高さの倍率」0.6 で下げる
    EXPECT_FLOAT_EQ(center.reboundArc.apexHeight, 1.15f * 0.6f);
    EXPECT_FLOAT_EQ(center.launchArc.direction.z, 1.0f);
}

namespace
{
    // +Z へ進む突進の外れ。面の右は +X
    ImpactInput MissAlongZ(float u, float v, NS::Obj::HitSensorShape shape)
    {
        ImpactInput input = BaseInput();
        input.slamVelocity = NS::Vector3{0.0f, 0.0f, 8.0f};
        input.awayDirection = NS::Vector3{0.0f, 0.0f, -1.0f};
        input.launchDirection = NS::Vector3{0.0f, 0.0f, 1.0f};
        input.faceU = u;
        input.faceV = v;
        input.bodyShape = shape;
        return input;
    }
} // namespace

// 外れの面の向きは面の上の位置から出す。丸は球の表面、四角は角を少し丸めた箱の表面 (角の鋭さ 6) として読む
TEST(ImpactOutcome, MissSurfaceNormalReadsTheFacePositionByShape)
{
    const NS::Vector3 forward{0.0f, 0.0f, 1.0f};
    const NS::Vector3 middle = GL::Level::MissSurfaceNormal(0.0f, 0.0f, 2.0f, forward);
    EXPECT_NEAR(middle.z, -1.0f, 0.0001f);
    // 右寄り・少し上の当たり。丸は上へ 0.40、四角は縁に沿って右へ逸れ、上へは 0.02
    const NS::Vector3 round = GL::Level::MissSurfaceNormal(0.85f, 0.4f, 2.0f, forward);
    const NS::Vector3 box = GL::Level::MissSurfaceNormal(0.85f, 0.4f, 6.0f, forward);
    EXPECT_NEAR(round.y, 0.40f, 0.005f);
    EXPECT_NEAR(box.y, 0.02f, 0.005f);
    EXPECT_GT(round.x, 0.0f);
    EXPECT_NEAR(round.Length(), 1.0f, 0.0001f);
    EXPECT_NEAR(box.Length(), 1.0f, 0.0001f);
    // 縁の外 (1 を超える位置) は縁で頭打ち
    const NS::Vector3 beyond = GL::Level::MissSurfaceNormal(1.4f, 0.0f, 2.0f, forward);
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

// 外れの相手は押し込む成分の 2 乗で短く飛び、弧は外れの高さの割合でさらに低い。真ん中の段は縮めない
TEST(ImpactOutcome, MissLaunchShrinksWithTheSquareOfThePushAndStaysLow)
{
    const ImpactTuning tuning = DefaultTuning();
    // 面の向き n = 0.8 右 + 0.6 手前。押し込む成分は 0.6
    const ImpactInput missInput = MissAlongZ(0.8f, 0.0f, NS::Obj::HitSensorShape::Sphere);
    const ImpactOutcome miss = ComputeImpactOutcome(missInput, tuning);
    EXPECT_NEAR(miss.launchArc.distance, 29.0f * 0.36f * missInput.launch.missDistanceRatio, 0.001f);
    EXPECT_NEAR(miss.launchArc.apexHeight, 2.0f * 0.36f * missInput.launch.missHeightRatio, 0.001f);

    ImpactInput center = MissAlongZ(0.8f, 0.0f, NS::Obj::HitSensorShape::Sphere);
    center.tier = HitTier::Center;
    const ImpactOutcome centerOutcome = ComputeImpactOutcome(center, tuning);
    EXPECT_FLOAT_EQ(centerOutcome.launchArc.distance, 29.0f);
    EXPECT_FLOAT_EQ(centerOutcome.launchArc.apexHeight, 2.0f);
}

// 下の縁の外れは地面へ叩きつけられ、外れの高さよりさらに低く跳ねる。上の縁は外れの高さより浮かない
TEST(ImpactOutcome, MissOffTheBottomBouncesLowerAndTheTopDoesNotFloat)
{
    const ImpactTuning tuning = DefaultTuning();
    const float plain =
        ComputeImpactOutcome(MissAlongZ(0.0f, 0.0f, NS::Obj::HitSensorShape::Sphere), tuning).reboundArc.apexHeight;
    const float bottom =
        ComputeImpactOutcome(MissAlongZ(0.0f, -0.8f, NS::Obj::HitSensorShape::Sphere), tuning).reboundArc.apexHeight;
    const float top =
        ComputeImpactOutcome(MissAlongZ(0.0f, 0.8f, NS::Obj::HitSensorShape::Sphere), tuning).reboundArc.apexHeight;
    // 真下を向く分 0.8 だけ、跳ねの割合 0.4 へ寄せる
    EXPECT_NEAR(bottom, plain * (1.0f - (1.0f - tuning.missSlamBounce) * 0.8f), 0.0001f);
    EXPECT_FLOAT_EQ(top, plain);
}

// 外れはかすった所の摩擦でねじれる。軸は 触れた面の向き × 滑る向き で、長さは端の近さ。真ん中の段はねじれない
TEST(ImpactOutcome, MissTwistsAroundTheSurfaceNormalCrossTheSlide)
{
    const ImpactTuning tuning = DefaultTuning();
    // 右の縁は相手の側が引きずられ、上から見て反時計回り (下向きの軸) に相手の方へ巻き込まれる
    const ImpactOutcome right = ComputeImpactOutcome(MissAlongZ(0.8f, 0.0f, NS::Obj::HitSensorShape::Sphere), tuning);
    ASSERT_TRUE(right.reboundArc.missTumble.has_value());
    EXPECT_NEAR(right.reboundArc.missTumble->twist.y, -0.8f, 0.001f);
    EXPECT_NEAR(right.reboundArc.missTumble->twist.x, 0.0f, 0.001f);
    EXPECT_NEAR(right.reboundArc.missTumble->twist.z, 0.0f, 0.001f);
    EXPECT_FLOAT_EQ(right.reboundArc.missTumble->power, BaseInput().power);
    // 上の縁は前へつんのめる回転 (上面が進む向きへ倒れる +X の軸)、下の縁は逆回転
    const ImpactOutcome top = ComputeImpactOutcome(MissAlongZ(0.0f, 0.8f, NS::Obj::HitSensorShape::Sphere), tuning);
    ASSERT_TRUE(top.reboundArc.missTumble.has_value());
    EXPECT_NEAR(top.reboundArc.missTumble->twist.x, 0.8f, 0.001f);
    const ImpactOutcome bottom = ComputeImpactOutcome(MissAlongZ(0.0f, -0.8f, NS::Obj::HitSensorShape::Sphere), tuning);
    ASSERT_TRUE(bottom.reboundArc.missTumble.has_value());
    EXPECT_NEAR(bottom.reboundArc.missTumble->twist.x, -0.8f, 0.001f);

    ImpactInput center = MissAlongZ(0.8f, 0.0f, NS::Obj::HitSensorShape::Sphere);
    center.tier = HitTier::Center;
    EXPECT_FALSE(ComputeImpactOutcome(center, tuning).reboundArc.missTumble.has_value());
}

// こすって止まる速さの倍率は (1 − 経過 ÷ N)^c。着いた速さに依らず N フレームで 0 になる
TEST(ImpactOutcome, MissSkidSpeedScaleFallsToZeroInTheSkidFrames)
{
    EXPECT_FLOAT_EQ(GL::Level::MissSkidSpeedScale(0, 18, 2.0f), 1.0f);
    EXPECT_FLOAT_EQ(GL::Level::MissSkidSpeedScale(9, 18, 2.0f), 0.25f);
    EXPECT_FLOAT_EQ(GL::Level::MissSkidSpeedScale(18, 18, 2.0f), 0.0f);
    EXPECT_FLOAT_EQ(GL::Level::MissSkidSpeedScale(30, 18, 2.0f), 0.0f);
    EXPECT_FLOAT_EQ(GL::Level::MissSkidSpeedScale(0, 0, 2.0f), 0.0f);
    // 減り方が有限の正でない時は直線
    EXPECT_FLOAT_EQ(GL::Level::MissSkidSpeedScale(9, 18, -1.0f), 0.5f);
}

// 相手の曲線は、相手が答えた飛び方で組む。同じ調整値でも、相手の高さと重力が違えば曲線が違う
TEST(ImpactOutcome, LaunchShapeComesFromTheInput)
{
    const ImpactTuning tuning = DefaultTuning();
    ImpactInput input = BaseInput();
    input.tier = HitTier::Center;
    input.launch.apexHeight = 3.0f;
    input.launch.riseGravity = 40.0f;
    input.launch.fallGravityScale = 2.0f;
    input.launch.apexBandSpeed = 0.5f;
    input.launch.apexBandGravityScale = 0.25f;

    const ImpactOutcome outcome = ComputeImpactOutcome(input, tuning);

    EXPECT_FLOAT_EQ(outcome.launchArc.apexHeight, 3.0f * outcome.launchScale);
    EXPECT_FLOAT_EQ(outcome.launchArc.riseGravity, 40.0f);
    EXPECT_FLOAT_EQ(outcome.launchArc.fallGravityScale, 2.0f);
    EXPECT_FLOAT_EQ(outcome.launchArc.apexBandSpeed, 0.5f);
    EXPECT_FLOAT_EQ(outcome.launchArc.apexBandGravityScale, 0.25f);
}
