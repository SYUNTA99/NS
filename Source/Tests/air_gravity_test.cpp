#include "Game/Level/AirGravity.h"
#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"

#include <gtest/gtest.h>

// 空中の重力の強さの選び方を縛る

namespace
{
    using GL::Level::AirGravity;
    using GL::Level::ChooseGravity;

    constexpr float k_Dt = 1.0f / 60.0f;
} // namespace

// 重力の強さの選び方: 上りは上昇重力、下りは下降重力、縦の速さの大きさが頂点滞空 Vy 未満の間は頂点滞空倍率を掛ける
TEST(AirGravityTest, ChooseGravityPicksRiseFallAndTheApexBand)
{
    const AirGravity gravity = GL::Player::PlayerParams{}.Gravity();
    EXPECT_FLOAT_EQ(ChooseGravity(gravity, 5.0f), -25.0f);
    EXPECT_FLOAT_EQ(ChooseGravity(gravity, 0.5f), -12.5f);
    EXPECT_FLOAT_EQ(ChooseGravity(gravity, -0.5f), -17.5f);
    EXPECT_FLOAT_EQ(ChooseGravity(gravity, -5.0f), -35.0f);
}

// 帯の境目の速さは帯の外
TEST(AirGravityTest, ChooseGravityLeavesTheBandEdgeOutside)
{
    const AirGravity gravity = GL::Player::PlayerParams{}.Gravity();
    EXPECT_FLOAT_EQ(ChooseGravity(gravity, gravity.apexSpeed), gravity.rise);
    EXPECT_FLOAT_EQ(ChooseGravity(gravity, -gravity.apexSpeed), gravity.fall);
}

// PlayerActor::Gravity と切り出した関数は同じ値を当てる。欄を変えても両方が一緒に変わる
TEST(AirGravityTest, PlayerActorGravityMatchesTheSharedChoice)
{
    for (int pass = 0; pass < 2; ++pass)
    {
        // 回ごとにシーンを分け、前の回の自機の状態を持ち越さない
        NS::Scene scene;
        PlayerActor* spawned = scene.SpawnActor<PlayerActor>("PlayerActor");
        ASSERT_NE(spawned, nullptr);
        PlayerActor& player = *spawned;
        if (pass == 1)
        {
            ASSERT_EQ(NS::ApplyJsonFields(
                          &player.Params(),
                          {{"上昇重力", -18.0f}, {"下降重力", -41.0f}, {"頂点滞空 Vy", 2.0f}, {"頂点滞空倍率", 0.3f}}),
                      0u);
        }
        const AirGravity gravity = player.Params().Gravity();
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
