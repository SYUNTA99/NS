#include "Game/Player.h"
#include "Game/Player/PlayerParams.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Windows/Clock.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    // 欄の秒を固定ステップのフレーム数にする。Player::AdvanceCharge と同じ丸め
    int FramesFor(float seconds)
    {
        return static_cast<int>(std::lround(seconds / NS::OS::FrameTimer::FixedDelta()));
    }

    // 前 (z) から右 (x) へ振れた角度 (ラジアン)
    float SwayAngle(const NS::Vector3& direction)
    {
        return std::atan2(direction.x, direction.z);
    }

    // カメラが z を向いた場面に自機を 1 体置く。狙う相手はいないので、揺れは欄「相手がいない時に直す距離」で角度に直る
    struct SwayScene
    {
        NS::Obj::Scene scene;
        Player* player = nullptr;

        void Load()
        {
            nlohmann::json doc = NS::Obj::MakeSceneJson();
            nlohmann::json entry = NS::Obj::MakeObjectJson();
            NS::Obj::SetObjectJsonClass(entry, "Player");
            NS::Obj::SetObjectJsonId(entry, 1);
            NS::Obj::SceneJsonObjects(doc).push_back(std::move(entry));
            scene.LoadJson(doc);
            player = static_cast<Player*>(scene.Objects().FindByObjectId(1));
            ASSERT_NE(player, nullptr);
            ASSERT_EQ(NS::Obj::ApplyJsonFields(player->Params(),
                                               {{"チャージしきい値秒", 0.2f},
                                                {"チャージ満タン秒", 1.0f},
                                                {"溜めすぎの秒数", 1.0f},
                                                {"紫の揺れの最大のずれ", 2.0f},
                                                {"相手がいない時に直す距離", 10.0f}}),
                      0u);
            ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f}), nullptr);
        }
    };
} // namespace

// 赤のうちは揺れず威力も溜めきりのまま。紫の間は狙いの線が左右に振れ、放すと放す前のフレームの線の向きへ出る
TEST(PlayerOverchargeSway, RedHoldsStillAndPurpleReleaseFollowsTheSwayedLine)
{
    SwayScene s;
    s.Load();
    Player& player = *s.player;
    const int full = FramesFor(1.0f);
    for (int held = 0; held < full; ++held)
    {
        player.Update(true);
    }
    NS::Game::Level::AimLine aim{};
    ASSERT_TRUE(player.TryGetAimLine(aim));
    EXPECT_FLOAT_EQ(SwayAngle(aim.direction), 0.0f);

    // 紫の前半は振れ幅が深さの 2 乗で小さい。後半で大きく振れる
    float earlyMax = 0.0f;
    for (int held = 0; held < FramesFor(0.25f); ++held)
    {
        player.Update(true);
        ASSERT_TRUE(player.TryGetAimLine(aim));
        earlyMax = std::max(earlyMax, std::fabs(SwayAngle(aim.direction)));
    }
    float lateMax = 0.0f;
    for (int held = 0; held < FramesFor(0.65f); ++held)
    {
        player.Update(true);
        ASSERT_TRUE(player.TryGetAimLine(aim));
        lateMax = std::max(lateMax, std::fabs(SwayAngle(aim.direction)));
    }
    const float edge = std::atan2(2.0f, 10.0f);
    EXPECT_GT(earlyMax, 0.0f);
    EXPECT_LT(earlyMax, edge * 0.1f);
    EXPECT_GT(lateMax, edge * 0.3f);
    EXPECT_LE(lateMax, edge + 0.0001f);
    EXPECT_FLOAT_EQ(aim.direction.y, 0.0f);

    const float overcharge = player.ChargeJudge().Overcharge01();
    player.Update(false);
    ASSERT_TRUE(player.IsBodySlamming());
    EXPECT_NEAR(SwayAngle(player.BodySlamDirection()), SwayAngle(aim.direction), 0.0001f);
    EXPECT_FLOAT_EQ(player.BodySlamCharge01(), 1.0f);
    EXPECT_FLOAT_EQ(player.BodySlamOvercharge01(), overcharge);
    // 自分で放した突進ではカメラを遅らせない
    EXPECT_FALSE(player.GetCameraTargetState().rebound.forcedSlamming);
}

// 紫になりきった時、揺れは端に来ていて、勝手に出た突進はその向きへ出る。どちらの端かは溜めすぎのたびに入れ替わる
TEST(PlayerOverchargeSway, ForcedLaunchLeavesAtAnEdgeThatAlternates)
{
    SwayScene s;
    s.Load();
    Player& player = *s.player;
    const float edge = std::atan2(2.0f, 10.0f);
    const int forced = FramesFor(1.0f) + FramesFor(1.0f);
    float sides[2] = {0.0f, 0.0f};
    for (int round = 0; round < 2; ++round)
    {
        player.ResetState();
        NS::Game::Level::AimLine aim{};
        for (int held = 1; held < forced; ++held)
        {
            player.Update(true);
        }
        ASSERT_TRUE(player.TryGetAimLine(aim));
        player.Update(true);
        ASSERT_TRUE(player.IsBodySlamming());
        const float angle = SwayAngle(player.BodySlamDirection());
        EXPECT_NEAR(angle, SwayAngle(aim.direction), 0.0001f);
        // 矢印の線は出る前のフレームの物なので、端の 1 フレーム手前
        EXPECT_GT(std::fabs(angle), edge * 0.9f);
        EXPECT_LE(std::fabs(angle), edge + 0.0001f);
        EXPECT_FLOAT_EQ(player.BodySlamOvercharge01(), 1.0f);
        sides[round] = angle;
        player.Update(false);
    }
    EXPECT_LT(sides[0] * sides[1], 0.0f);
}

// 紫の威力は溜めきりの倍率に、紫の深さで 1 から欄「紫の威力の上限」まで上がる倍率を掛ける
TEST(PlayerOverchargeSway, PurplePowerRisesToTheCapFromTheFullCharge)
{
    Player player;
    player.Init();
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player.Params(), {{"紫の威力の上限", 1.5f}}), 0u);
    const NS::Game::Player::PlayerParams& params = player.Params();
    const float full = params.ChargeFactorFor(1.0f);
    EXPECT_FLOAT_EQ(params.ChargeFactorFor(1.0f, 0.0f), full);
    EXPECT_FLOAT_EQ(params.ChargeFactorFor(1.0f, 0.5f), full * 1.25f);
    EXPECT_FLOAT_EQ(params.ChargeFactorFor(1.0f, 1.0f), full * 1.5f);
    EXPECT_FLOAT_EQ(params.ChargeFactorFor(0.5f, 0.0f), params.ChargeFactorFor(0.5f));
}

// 揺れが端を通るフレームに 1 回ずつ拍が立つ。紫の 3 秒に 9 回で、最後の拍は勝手に出るフレーム。拍ごとに端が入れ替わる
TEST(PlayerOverchargeSway, EdgeBeatsCountDownToTheForcedLaunch)
{
    SwayScene s;
    s.Load();
    Player& player = *s.player;
    ASSERT_EQ(NS::Obj::ApplyJsonFields(
                  player.Params(),
                  {{"溜めすぎの秒数", 3.0f}, {"紫の揺れの始めの速さ", 0.8f}, {"紫の揺れの終わりの速さ", 2.6f}}),
              0u);
    const int full = FramesFor(1.0f);
    const int forced = full + FramesFor(3.0f);
    int beats = 0;
    int lastBeat = -1;
    float lastSide = 0.0f;
    bool alternates = true;
    for (int held = 1; held <= forced; ++held)
    {
        player.Update(true);
        if (!player.ChargeSwayReachedEdge())
        {
            continue;
        }
        ++beats;
        lastBeat = held;
        const float side = player.ChargeSwayOffset();
        EXPECT_NE(side, 0.0f);
        if (lastSide * side > 0.0f)
        {
            alternates = false;
        }
        lastSide = side;
    }
    EXPECT_EQ(beats, 9);
    EXPECT_EQ(lastBeat, forced);
    EXPECT_TRUE(alternates);
    EXPECT_TRUE(player.IsBodySlamming());
    // 出た後は拍が立たない
    for (int frame = 0; frame < FramesFor(1.0f); ++frame)
    {
        player.Update(true);
        EXPECT_FALSE(player.ChargeSwayReachedEdge());
    }
}
