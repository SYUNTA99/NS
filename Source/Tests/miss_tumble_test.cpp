#include "Game/Player.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "Game/Player/ReboundArc.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

// 外れの反動は、かすった所の摩擦の軸でねじれ、軸がぶれて回る。着地の後は決めたフレーム数でこすって止まる

namespace
{
    constexpr float k_FrameSeconds = 1.0f / 60.0f;

    // 床の上に自機 (id 1) を置き、接地させてから丸める
    Player* PlaceTumblePlayer(NS::Obj::Scene& scene)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json player = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(player, "Player");
        NS::Obj::SetObjectJsonId(player, 1);
        NS::Obj::SetObjectPosition(player, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(player));
        scene.LoadJson(doc);
        NS::OBB floor{};
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
        Player* placed = NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
        if (placed == nullptr)
        {
            return nullptr;
        }
        for (int frame = 0; frame < 60; ++frame)
        {
            placed->Update(false);
        }
        placed->SetCurled(true);
        return placed;
    }

    // 右の縁で外した反動。+Z へ進み、右へ逸れる
    NS::Game::Player::ReboundArc MissArc(float distance)
    {
        return NS::Game::Player::ReboundArc{
            .direction = NS::Vector3{0.96f, 0.0f, 0.28f},
            .apexHeight = 0.35f,
            .distance = distance,
            .missTumble = NS::Game::Player::MissTumble{.twist = NS::Vector3{0.0f, -0.8f, 0.0f}, .power = 1.0f}};
    }

    float LateralSpeed(const Player& player)
    {
        return player.Body().LateralVelocity().Length();
    }
} // namespace

// 外れの反動は着地の後、着いた水平の速さに (1 − 経過 ÷ N)^c を掛けてこすって止まる。着いた速さに依らず
// N フレームで操作が戻り、その間は玉のまま
TEST(MissTumble, MissReboundSkidsToAStopInTheSkidFrames)
{
    for (const float distance : {0.8f, 3.0f})
    {
        SCOPED_TRACE(distance);
        NS::Obj::Scene scene;
        Player* player = PlaceTumblePlayer(scene);
        ASSERT_NE(player, nullptr);
        ASSERT_EQ(
            NS::Obj::ApplyJsonFields(
                player->Params(), {{"外れのこすって止まるまでのフレーム数", 10}, {"外れのこすって止まる減り方", 2.0f}}),
            0u);
        ASSERT_TRUE(player->BeginRebound(MissArc(distance)));
        bool landed = false;
        for (int frame = 0; frame < 120 && !landed; ++frame)
        {
            player->Update(false);
            landed = !player->IsRebounding();
        }
        ASSERT_TRUE(landed);
        ASSERT_TRUE(player->IsSkidding());
        EXPECT_TRUE(player->IsCurled());

        std::vector<float> speeds;
        while (player->IsSkidding() && speeds.size() < 30)
        {
            player->Update(false);
            speeds.push_back(LateralSpeed(*player));
            // 止まりきったフレームに立ちへ移り、丸まりも解ける
            EXPECT_EQ(player->IsCurled(), player->IsSkidding());
        }
        // 着いたフレームに 0 フレーム目、続く 10 フレームで 0 になって立ちへ戻る
        ASSERT_EQ(speeds.size(), 10u);
        ASSERT_GT(speeds.front(), 0.0f);
        for (std::size_t step = 1; step < speeds.size(); ++step)
        {
            const float expected = speeds.front() * std::pow(1.0f - static_cast<float>(step + 1) / 10.0f, 2.0f) /
                                   std::pow(1.0f - 1.0f / 10.0f, 2.0f);
            EXPECT_NEAR(speeds[step], expected, 1.0e-4f);
        }
        EXPECT_NEAR(speeds.back(), 0.0f, 1.0e-6f);
        EXPECT_FALSE(player->IsSkidding());
    }
}

// 真ん中の反動と、止まるまでのフレーム数が 0 の外れの反動は、着地したフレームに立ちへ戻る
TEST(MissTumble, CenterReboundAndZeroSkidFramesLandWithoutSkidding)
{
    for (const bool miss : {false, true})
    {
        SCOPED_TRACE(miss);
        NS::Obj::Scene scene;
        Player* player = PlaceTumblePlayer(scene);
        ASSERT_NE(player, nullptr);
        NS::Game::Player::ReboundArc arc = MissArc(3.0f);
        if (miss)
        {
            ASSERT_EQ(NS::Obj::ApplyJsonFields(player->Params(), {{"外れのこすって止まるまでのフレーム数", 0}}), 0u);
        }
        else
        {
            arc.missTumble.reset();
        }
        ASSERT_TRUE(player->BeginRebound(arc));
        bool landed = false;
        for (int frame = 0; frame < 120 && !landed; ++frame)
        {
            player->Update(false);
            landed = !player->IsRebounding();
        }
        ASSERT_TRUE(landed);
        EXPECT_FALSE(player->IsSkidding());
    }
}

// 外れの玉は欄のフレーム数で ねじれ へ寄って回る。ぶれの角度を 0 にすると軸はねじれの軸のまま、
// 角度を付けると軸はねじれの軸からその角度だけ傾く
TEST(MissTumble, TumbleBlendsIntoTheTwistAndTiltsByTheWobble)
{
    for (const float wobble : {0.0f, 30.0f})
    {
        SCOPED_TRACE(wobble);
        NS::Obj::Scene scene;
        Player* player = PlaceTumblePlayer(scene);
        ASSERT_NE(player, nullptr);
        ASSERT_EQ(NS::Obj::ApplyJsonFields(player->Appearance(),
                                           {{"外れの縁でのねじれの回転数", 2.0f},
                                            {"外れで当たる前の回転を引き継ぐ割合", 0.0f},
                                            {"外れの回転を寄せるフレーム数", 4},
                                            {"外れの軸のぶれの角度", wobble}}),
                  0u);
        ASSERT_TRUE(player->BeginRebound(MissArc(3.0f)));
        std::vector<float> degrees;
        for (int frame = 0; frame < 6; ++frame)
        {
            player->Update(false);
            degrees.push_back(player->Appearance().SpinDegreesThisFrame());
        }
        // 立っていた玉は回っていないので 0 から寄る。4 フレーム目で ねじれの軸 0.8 × 2 回/秒 × 威力 1
        const float full = 0.8f * 2.0f * 360.0f * k_FrameSeconds;
        EXPECT_NEAR(degrees[1], full * 0.5f, 1.0e-3f);
        EXPECT_NEAR(degrees[3], full, 1.0e-3f);
        EXPECT_NEAR(degrees[5], full, 1.0e-3f);
        const NS::Vector3 axis = player->Appearance().SpinAxis();
        EXPECT_NEAR(axis.Length(), 1.0f, 1.0e-4f);
        EXPECT_NEAR(-axis.y, std::cos(NS::ToRadians(NS::Degrees{wobble}).value), 1.0e-4f);
    }
}

// 溜めて外したほど大きく振り回される。突進の回転を欄の割合だけ引き継ぎ、ねじれに足す
TEST(MissTumble, TumbleCarriesTheSlamSpin)
{
    NS::Obj::Scene scene;
    Player* player = PlaceTumblePlayer(scene);
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::Obj::ApplyJsonFields(player->Appearance(),
                                       {{"外れの縁でのねじれの回転数", 0.0f},
                                        {"外れで当たる前の回転を引き継ぐ割合", 0.5f},
                                        {"外れの回転を寄せるフレーム数", 1},
                                        {"外れの軸のぶれの角度", 0.0f}}),
              0u);
    player->RequestBodySlam(0.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    player->Update(false);
    ASSERT_TRUE(player->IsBodySlamming());
    const float slamDegrees = player->Appearance().SpinDegreesThisFrame();
    const NS::Vector3 slamAxis = player->Appearance().SpinAxis();
    ASSERT_GT(slamDegrees, 0.0f);

    ASSERT_TRUE(player->BeginRebound(MissArc(3.0f)));
    player->Update(false);
    EXPECT_NEAR(player->Appearance().SpinDegreesThisFrame(), slamDegrees * 0.5f, 1.0e-3f);
    EXPECT_NEAR(player->Appearance().SpinAxis().Dot(slamAxis), 1.0f, 1.0e-4f);
}
