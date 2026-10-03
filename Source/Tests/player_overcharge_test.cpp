#include "Game/Player.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Platform/Clock.h"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    // 欄の秒を固定ステップのフレーム数にする。Player::AdvanceCharge と同じ丸め
    int FramesFor(float seconds)
    {
        return static_cast<int>(std::lround(seconds / NS::Platform::FrameTimer::FixedDelta()));
    }

    // しきい値 0.2 秒・満タン 1 秒・溜めすぎ 0.5 秒。強制で出るのは押し始めから 1.5 秒のフレーム
    void UseShortOvercharge(Player& player)
    {
        ASSERT_EQ(
            NS::Obj::ApplyJsonFields(
                player.Params(), {{"チャージしきい値秒", 0.2f}, {"チャージ満タン秒", 1.0f}, {"溜めすぎの秒数", 0.5f}}),
            0u);
    }
} // namespace

// 溜めきりの後は溜めすぎが 0 から 1 へ進み、1 になったフレームに押したままでも 1 回だけ出る
TEST(PlayerOvercharge, ForcedLaunchFiresOnceAndThenStopsTheChargeShow)
{
    Player player;
    UseShortOvercharge(player);
    player.SetDesiredMove(NS::Core::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    const int full = FramesFor(1.0f);
    const int forced = full + FramesFor(0.5f);
    const float maxSpeed = player.MaxSpeed();

    for (int held = 1; held <= full; ++held)
    {
        player.Update(true);
    }
    ASSERT_TRUE(player.ChargeJudge().IsChargeFull());
    EXPECT_FLOAT_EQ(player.ChargeJudge().Overcharge01(), 0.0f);
    for (int held = full + 1; held < forced; ++held)
    {
        player.Update(true);
        EXPECT_FALSE(player.IsBodySlamming());
    }
    EXPECT_GT(player.ChargeJudge().Overcharge01(), 0.9f);
    EXPECT_LT(player.ChargeJudge().Overcharge01(), 1.0f);

    player.Update(true);
    EXPECT_TRUE(player.IsBodySlamming());
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 1.0f);
    // 勝手に出た突進の間は、追従カメラが遅れて付いていく
    EXPECT_TRUE(player.GetCameraTargetState().rebound.forcedSlamming);

    // 出た後は押したままでも溜めの見せ方を出さない。押している事実と丸まりは続く
    player.Update(true);
    EXPECT_TRUE(player.ChargeJudge().IsHeld());
    EXPECT_FALSE(player.ChargeJudge().IsHoldingCharge());
    EXPECT_FALSE(player.ChargeJudge().IsCharging());
    EXPECT_FLOAT_EQ(player.ChargeJudge().Charge01(), 0.0f);
    EXPECT_FLOAT_EQ(player.ChargeJudge().Overcharge01(), 0.0f);
    EXPECT_TRUE(player.IsCurled());
    EXPECT_FLOAT_EQ(player.StanceHeight(), 1.0f);
    EXPECT_FLOAT_EQ(player.MaxSpeed(), maxSpeed);
    EXPECT_FALSE(player.GetCameraTargetState().charge.held);

    // 突進が終わって押し続けても 2 本目は出ない。放しても何も出ない
    int slams = 0;
    bool wasSlamming = true;
    for (int frame = 0; frame < FramesFor(3.0f); ++frame)
    {
        player.Update(true);
        if (player.IsBodySlamming() && !wasSlamming)
        {
            ++slams;
        }
        wasSlamming = player.IsBodySlamming();
    }
    player.Update(false);
    for (int frame = 0; frame < FramesFor(0.5f); ++frame)
    {
        if (player.IsBodySlamming() && !wasSlamming)
        {
            ++slams;
        }
        wasSlamming = player.IsBodySlamming();
        player.Update(false);
    }
    EXPECT_EQ(slams, 0);
}

// 出せない間に溜めすぎきった時は消えずに待ち、出せるようになったフレームに出る
TEST(PlayerOvercharge, ForcedLaunchWaitsUntilASlamCanStart)
{
    Player player;
    UseShortOvercharge(player);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player.Params(), {{"突進距離", 1000.0f}}), 0u);
    player.SetDesiredMove(NS::Core::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    // 溜めて放した長い突進の間に押し直し、突進の最中に溜めすぎきらせる
    for (int held = 0; held < FramesFor(1.0f); ++held)
    {
        player.Update(true);
    }
    player.Update(false);
    ASSERT_TRUE(player.IsBodySlamming());
    for (int held = 0; held < FramesFor(1.5f) + 5; ++held)
    {
        player.Update(true);
    }
    ASSERT_TRUE(player.IsBodySlamming());
    EXPECT_TRUE(player.ChargeJudge().IsAwaitingLaunch());
    EXPECT_TRUE(player.ChargeJudge().IsHoldingCharge());
    EXPECT_TRUE(player.ChargeJudge().IsChargeFull());
    EXPECT_FLOAT_EQ(player.ChargeJudge().Overcharge01(), 1.0f);

    // 突進を打ち切っても、空中で出した突進の後は着地まで出せないので待ち続ける
    player.CancelBodySlam();
    for (int frame = 0; frame < FramesFor(0.5f); ++frame)
    {
        player.Update(true);
        EXPECT_FALSE(player.IsBodySlamming());
    }
    EXPECT_TRUE(player.ChargeJudge().IsAwaitingLaunch());
    // やり直しで空中の 1 発の印が消えて出せるようになったフレームに出る
    player.ResetState();
    player.SetDesiredMove(NS::Core::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    player.Update(true);
    EXPECT_TRUE(player.IsBodySlamming());
    player.Update(true);
    EXPECT_FALSE(player.ChargeJudge().IsAwaitingLaunch());
    EXPECT_FALSE(player.ChargeJudge().IsHoldingCharge());
}

// 待つ間に放した時は何も控えず、判定は使い切りへ移らない
TEST(PlayerOvercharge, ReleasingWhileWaitingFiresNothingNew)
{
    Player player;
    UseShortOvercharge(player);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player.Params(), {{"突進距離", 1000.0f}}), 0u);
    player.SetDesiredMove(NS::Core::Vector3{1.0f, 0.0f, 0.0f}, 1.0f);
    for (int held = 0; held < FramesFor(1.0f); ++held)
    {
        player.Update(true);
    }
    player.Update(false);
    for (int held = 0; held < FramesFor(1.5f) + 5; ++held)
    {
        player.Update(true);
    }
    ASSERT_TRUE(player.ChargeJudge().IsAwaitingLaunch());
    player.Update(false);
    EXPECT_FALSE(player.ChargeJudge().IsHeld());
    EXPECT_FALSE(player.ChargeJudge().IsAwaitingLaunch());
    player.CancelBodySlam();
    for (int frame = 0; frame < FramesFor(0.5f); ++frame)
    {
        player.Update(false);
        EXPECT_FALSE(player.IsBodySlamming());
    }
}
