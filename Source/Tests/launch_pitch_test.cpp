#include "Game/Player.h"
#include "Game/Player/LaunchPitch.h"
#include "Game/Player/PlayerGravity.h"
#include "Game/Player/PlayerParams.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

// 溜めて放つ突進の上下の速さ (LaunchPitch) と、自機の重力の強さの選び方を縛る

namespace
{
    using GL::Player::ChooseGravity;
    using GL::Player::LaunchHeightAt;
    using GL::Player::LaunchPath;
    using GL::Player::LaunchPitch;
    using GL::Player::LaunchPitchDesc;
    using GL::Player::LaunchPitchResult;
    using GL::Player::PlayerGravity;

    constexpr float k_Dt = 1.0f / 60.0f;
    // 二分探索は 1 mm まで詰める。浮動小数の丸めを足して 2 mm で見る
    constexpr float k_HeightTolerance = 0.002f;

    // 同梱の自機の玉の中心の高さ (床 0 の上の半径 0.65 の玉) から、突進速度 20 m/s で放つ
    LaunchPitchDesc GroundDesc(float targetHeight, float contactDistance)
    {
        LaunchPitchDesc desc;
        desc.ballHeight = 0.65f;
        desc.targetHeight = targetHeight;
        desc.contactDistance = contactDistance;
        desc.horizontalSpeed = 20.0f;
        desc.gravity = PlayerGravity{};
        desc.maxAngleDegrees = 40.0f;
        desc.grounded = true;
        desc.dt = k_Dt;
        return desc;
    }

    // 求めた上下の速さで放った玉が、触れる所で居る高さ
    float ArrivalHeight(const LaunchPitchDesc& desc, float verticalSpeed)
    {
        const LaunchPath path{.horizontalSpeed = desc.horizontalSpeed,
                              .verticalSpeed = verticalSpeed,
                              .gravity = desc.gravity,
                              .dt = desc.dt,
                              .grounded = desc.grounded};
        return desc.ballHeight + LaunchHeightAt(path, desc.contactDistance);
    }

    float AngleDegrees(const LaunchPitchDesc& desc, float verticalSpeed)
    {
        return NS::RadiansToDegrees(std::atan2(verticalSpeed, desc.horizontalSpeed));
    }
} // namespace

// 重力の強さの選び方: 上りは上昇重力、下りは下降重力、縦の速さの大きさが頂点滞空 Vy 未満の間は頂点滞空倍率を掛ける
TEST(LaunchPitchTest, ChooseGravityPicksRiseFallAndTheApexBand)
{
    const PlayerGravity gravity{};
    EXPECT_FLOAT_EQ(ChooseGravity(gravity, 5.0f), -25.0f);
    EXPECT_FLOAT_EQ(ChooseGravity(gravity, 0.5f), -12.5f);
    EXPECT_FLOAT_EQ(ChooseGravity(gravity, -0.5f), -17.5f);
    EXPECT_FLOAT_EQ(ChooseGravity(gravity, -5.0f), -35.0f);
}

TEST(LaunchPitchTest, PredictionBudgetBelongsToThePath)
{
    LaunchPath path;
    path.horizontalSpeed = 1.0f;
    path.verticalSpeed = 1.0f;
    path.gravity = PlayerGravity{.rise = 0.0f, .fall = 0.0f, .apexSpeed = 0.0f, .apexScale = 1.0f};
    path.dt = 0.1f;
    path.maxFrames = 2;
    EXPECT_NEAR(LaunchHeightAt(path, 1.0f), 0.2f, 0.00001f);
    path.maxFrames = 20;
    EXPECT_NEAR(LaunchHeightAt(path, 1.0f), 1.0f, 0.00001f);
}

TEST(LaunchPitchTest, SearchStopsAtRepresentablePrecisionWithTinyTolerance)
{
    LaunchPitchDesc desc = GroundDesc(1.4321f, 8.0f);
    desc.heightTolerance = std::numeric_limits<float>::denorm_min();
    const LaunchPitchResult result = LaunchPitch(desc);
    ASSERT_TRUE(result.reachable);
    EXPECT_TRUE(std::isfinite(result.verticalSpeed));
    EXPECT_NEAR(ArrivalHeight(desc, result.verticalSpeed), desc.targetHeight, 0.00001f);
}

// Player::Gravity と切り出した関数は同じ値を当てる。欄を変えても両方が一緒に変わる
TEST(LaunchPitchTest, PlayerGravityMatchesTheSharedChoice)
{
    for (int pass = 0; pass < 2; ++pass)
    {
        Player player;
        player.Init();
        if (pass == 1)
        {
            ASSERT_EQ(NS::Obj::ApplyJsonFields(
                          player.Params(),
                          {{"上昇重力", -18.0f}, {"下降重力", -41.0f}, {"頂点滞空 Vy", 2.0f}, {"頂点滞空倍率", 0.3f}}),
                      0u);
        }
        const PlayerGravity gravity = player.Params().Gravity();
        for (const float vertical : {6.0f, 1.5f, 0.5f, -0.5f, -1.5f, -6.0f})
        {
            SCOPED_TRACE(pass);
            SCOPED_TRACE(vertical);
            player.Body().SetVerticalVelocity(vertical);
            player.Gravity(k_Dt);
            EXPECT_FLOAT_EQ(player.Body().VerticalVelocity(), vertical + ChooseGravity(gravity, vertical) * k_Dt);
        }
    }
}

// 大きい相手へ地上から放つと上向きに出て、触れる所で赤の高さに着く
TEST(LaunchPitchTest, TallTargetFromTheGroundLaunchesUpwardToTheRedHeight)
{
    const LaunchPitchDesc desc = GroundDesc(2.0f, 5.0f);
    const LaunchPitchResult result = LaunchPitch(desc);
    ASSERT_TRUE(result.reachable);
    EXPECT_GT(result.verticalSpeed, 0.0f);
    EXPECT_LE(AngleDegrees(desc, result.verticalSpeed), 40.0f);
    EXPECT_NEAR(ArrivalHeight(desc, result.verticalSpeed), 2.0f, k_HeightTolerance);
}

// 上限の角度で切る。40 度で届かない高さは縦の速さ 0 で水平に放つ。上限を上げれば同じ相手に届く
TEST(LaunchPitchTest, TargetBeyondTheAngleLimitLaunchesLevel)
{
    LaunchPitchDesc desc = GroundDesc(6.0f, 5.0f);
    const LaunchPitchResult limited = LaunchPitch(desc);
    EXPECT_FALSE(limited.reachable);
    EXPECT_FLOAT_EQ(limited.verticalSpeed, 0.0f);

    desc.maxAngleDegrees = 60.0f;
    const LaunchPitchResult wider = LaunchPitch(desc);
    ASSERT_TRUE(wider.reachable);
    EXPECT_GT(AngleDegrees(desc, wider.verticalSpeed), 40.0f);
    EXPECT_NEAR(ArrivalHeight(desc, wider.verticalSpeed), 6.0f, k_HeightTolerance);
}

// 地上では下へ向けない (床があるので水平)。空中では同じ相手へ下へ向ける
TEST(LaunchPitchTest, GroundNeverAimsDownButTheAirDoes)
{
    LaunchPitchDesc desc = GroundDesc(0.3f, 4.0f);
    const LaunchPitchResult ground = LaunchPitch(desc);
    EXPECT_FALSE(ground.reachable);
    EXPECT_FLOAT_EQ(ground.verticalSpeed, 0.0f);

    desc.ballHeight = 3.0f;
    desc.targetHeight = 1.0f;
    desc.grounded = false;
    const LaunchPitchResult air = LaunchPitch(desc);
    ASSERT_TRUE(air.reachable);
    EXPECT_LT(air.verticalSpeed, 0.0f);
    EXPECT_GE(AngleDegrees(desc, air.verticalSpeed), -40.0f);
    EXPECT_NEAR(ArrivalHeight(desc, air.verticalSpeed), 1.0f, k_HeightTolerance);

    // 下向きも同じ上限で切る
    desc.contactDistance = 2.0f;
    desc.targetHeight = -3.0f;
    const LaunchPitchResult steep = LaunchPitch(desc);
    EXPECT_FALSE(steep.reachable);
    EXPECT_FLOAT_EQ(steep.verticalSpeed, 0.0f);
}

// 縦の速さ 0 の道筋: 地上は床の上を水平に、空中は放った高さから落ちる
TEST(LaunchPitchTest, LevelPathStaysOnTheFloorOrFallsInTheAir)
{
    LaunchPath path{.horizontalSpeed = 20.0f, .verticalSpeed = 0.0f, .gravity = PlayerGravity{}, .dt = k_Dt};
    path.grounded = true;
    EXPECT_FLOAT_EQ(LaunchHeightAt(path, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(LaunchHeightAt(path, 6.0f), 0.0f);
    path.grounded = false;
    EXPECT_FLOAT_EQ(LaunchHeightAt(path, 0.0f), 0.0f);
    EXPECT_LT(LaunchHeightAt(path, 6.0f), LaunchHeightAt(path, 3.0f));
    EXPECT_LT(LaunchHeightAt(path, 3.0f), 0.0f);
    // 1 フレーム目は頂点付近の重力 (下降重力 × 頂点滞空倍率) で落ちてから進む。フレームの間は直線
    const float firstFrame = 20.0f * k_Dt;
    EXPECT_NEAR(LaunchHeightAt(path, firstFrame), -17.5f * k_Dt * k_Dt, 1e-6f);
    EXPECT_NEAR(LaunchHeightAt(path, firstFrame * 0.5f), -17.5f * k_Dt * k_Dt * 0.5f, 1e-6f);
}

// 有限でない入力は届かない扱いで 0
TEST(LaunchPitchTest, NonFiniteInputLaunchesLevel)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    LaunchPitchDesc desc = GroundDesc(nan, 5.0f);
    EXPECT_FALSE(LaunchPitch(desc).reachable);
    desc = GroundDesc(2.0f, nan);
    EXPECT_FALSE(LaunchPitch(desc).reachable);
    desc = GroundDesc(2.0f, 5.0f);
    desc.horizontalSpeed = 0.0f;
    EXPECT_FALSE(LaunchPitch(desc).reachable);
    desc = GroundDesc(2.0f, 5.0f);
    desc.maxAngleDegrees = nan;
    EXPECT_FALSE(LaunchPitch(desc).reachable);
    EXPECT_FLOAT_EQ(LaunchPitch(desc).verticalSpeed, 0.0f);
}

// 放った瞬間の縦の速さ: 添えた値で出る。添えない溜めた突進は、空中で上がっている途中でも 0 から
TEST(LaunchPitchTest, ChargedSlamStartsFromTheGivenVerticalSpeed)
{
    const NS::Vector3 forward{0.0f, 0.0f, 1.0f};
    {
        Player player;
        player.Init();
        player.Body().SetVerticalVelocity(8.0f);
        player.RequestBodySlam(1.0f, forward);
        ASSERT_TRUE(player.BodySlam());
        EXPECT_FLOAT_EQ(player.Body().VerticalVelocity(), 0.0f);
    }
    {
        Player player;
        player.Init();
        player.Body().SetVerticalVelocity(8.0f);
        player.RequestBodySlam(1.0f, forward, 4.5f);
        ASSERT_TRUE(player.BodySlam());
        EXPECT_FLOAT_EQ(player.Body().VerticalVelocity(), 4.5f);
    }
    {
        // 向きを添えない要求は前の上下の速さを残さない
        Player player;
        player.Init();
        player.RequestBodySlam(1.0f, forward, 4.5f);
        player.RequestBodySlam(1.0f);
        player.Body().SetVerticalVelocity(8.0f);
        player.SetDesiredMove(forward, 1.0f);
        ASSERT_TRUE(player.BodySlam());
        EXPECT_FLOAT_EQ(player.Body().VerticalVelocity(), 0.0f);
    }
    {
        // 通常突進は今のまま「通常突進の上向き初速」で跳ぶ
        Player player;
        player.Init();
        player.RequestBodySlam(0.0f, forward, 4.5f);
        ASSERT_TRUE(player.BodySlam());
        EXPECT_FLOAT_EQ(player.Body().VerticalVelocity(), 3.0f);
    }
}
