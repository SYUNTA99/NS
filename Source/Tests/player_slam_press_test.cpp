#include "Game/Level/ImpactResolver.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Object/Components/Body.h"
#include "NSlib/Object/Components/TransformComponent.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

// 突進を出してから反動に入るまでの押しは、次の突進に持ち越さない
// 反動の間の押しは空中の 1 発になる。空中の 1 発は 1 回の空中に 1 回で、地面から出した突進は数えない

namespace
{
    const NS::Vector3 k_Forward{0.0f, 0.0f, 1.0f};

    // 自機 (id 1) を高さ height に置く。withRock なら前に置物 (id 2) を置く
    Player* PlacePressScene(NS::Obj::Scene& scene, bool withRock, float height)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json player = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(player, "Player");
        NS::Obj::SetObjectJsonId(player, 1);
        NS::Obj::SetObjectPosition(player, NS::Vector3{0.0f, height, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(player));
        if (withRock)
        {
            nlohmann::json rock = NS::Obj::MakeObjectJson();
            NS::Obj::SetObjectJsonClass(rock, "MapObj");
            NS::Obj::SetObjectJsonId(rock, 2);
            NS::Obj::SetObjectPosition(rock, NS::Vector3{0.0f, 0.5f, 0.6f});
            NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        }
        scene.LoadJson(doc);
        NS::OBB floor{};
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, NS::Vector3{}, k_Forward);
        return NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    }

    NS::Game::Level::MapObj* RockOf(NS::Obj::Scene& scene)
    {
        return NS::Obj::Cast<NS::Game::Level::MapObj>(scene.Objects().FindByObjectId(2));
    }

    // 接地するまで回す
    void Settle(Player& player)
    {
        for (int frame = 0; frame < 60 && !player.Body().IsGrounded(); ++frame)
        {
            player.Update(false);
        }
    }

    // 1 フレーム進め、そのフレームに突進が始まった場合 true を返す
    bool StepAndSeeSlamStart(Player& player, NS::Game::Level::MapObj* rock)
    {
        const bool before = player.IsBodySlamming();
        player.Update(false);
        if (rock != nullptr)
        {
            rock->Update();
        }
        return !before && player.IsBodySlamming();
    }
} // namespace

// 突進中に押しても、空振りで突進が終わった後に突進が出ない
TEST(PlayerSlamPress, PressDuringASlamDoesNotFireAfterTheMiss)
{
    NS::Obj::Scene scene;
    Player* player = PlacePressScene(scene, false, 1.0f);
    ASSERT_NE(player, nullptr);
    Settle(*player);
    ASSERT_TRUE(player->Body().IsGrounded());
    player->RequestBodySlam(0.0f, k_Forward);
    int starts = 0;
    bool pressed = false;
    for (int frame = 0; frame < 120; ++frame)
    {
        if (StepAndSeeSlamStart(*player, nullptr))
        {
            ++starts;
        }
        // 突進の 3 フレーム目に押す。終わるまで残っていれば、終わった後の先行入力で 2 本目が出る
        if (!pressed && frame == 2)
        {
            ASSERT_TRUE(player->IsBodySlamming());
            player->RequestBodySlam(0.0f);
            pressed = true;
        }
    }
    EXPECT_TRUE(pressed);
    EXPECT_EQ(starts, 1);
}

// 当たった後の止めの間に押しても、明けに突進が出ない
TEST(PlayerSlamPress, PressDuringTheHitStopDoesNotFireOnTheRelease)
{
    NS::Obj::Scene scene;
    Player* player = PlacePressScene(scene, true, 1.0f);
    ASSERT_NE(player, nullptr);
    NS::Game::Level::MapObj* rock = RockOf(scene);
    ASSERT_NE(rock, nullptr);
    Settle(*player);
    player->RequestBodySlam(1.0f, k_Forward);
    int starts = 0;
    bool pressed = false;
    for (int frame = 0; frame < 120; ++frame)
    {
        if (StepAndSeeSlamStart(*player, rock))
        {
            ++starts;
        }
        if (!pressed && player->Resolver().IsHitStopping())
        {
            player->RequestBodySlam(0.0f);
            pressed = true;
        }
    }
    EXPECT_TRUE(pressed);
    EXPECT_EQ(starts, 1);
}

// 地面から出した突進の反動の間に押すと、空中の 1 発が出る。浮いて当てた通常突進と、上向きに放った溜めた突進でも出る
TEST(PlayerSlamPress, PressDuringTheReboundOfAGroundSlamFiresTheAirShot)
{
    for (const float charge01 : {0.0f, 1.0f})
    {
        SCOPED_TRACE(charge01);
        NS::Obj::Scene scene;
        Player* player = PlacePressScene(scene, true, 1.0f);
        ASSERT_NE(player, nullptr);
        NS::Game::Level::MapObj* rock = RockOf(scene);
        ASSERT_NE(rock, nullptr);
        Settle(*player);
        ASSERT_TRUE(player->Body().IsGrounded());
        // 溜めた突進は上向きに放ち、当たるまで接地させない
        player->RequestBodySlam(charge01, k_Forward, 3.0f);
        int starts = 0;
        bool pressed = false;
        bool groundedBeforeTheRebound = false;
        bool firedFromTheRebound = false;
        for (int frame = 0; frame < 120 && !firedFromTheRebound; ++frame)
        {
            const bool rebounding = player->IsRebounding();
            if (StepAndSeeSlamStart(*player, rock))
            {
                ++starts;
                firedFromTheRebound = rebounding;
            }
            if (starts == 1 && !pressed && player->IsBodySlamming() && player->Body().IsGrounded())
            {
                groundedBeforeTheRebound = true;
            }
            if (!pressed && player->IsRebounding())
            {
                player->RequestBodySlam(0.0f, k_Forward);
                pressed = true;
            }
        }
        EXPECT_FALSE(groundedBeforeTheRebound);
        EXPECT_TRUE(pressed);
        EXPECT_TRUE(firedFromTheRebound);
        EXPECT_EQ(starts, 2);
    }
}

// 空中で出した突進の後は、着地するまで次の突進が出ない
TEST(PlayerSlamPress, AnAirLaunchedSlamBlocksTheNextUntilLanding)
{
    NS::Obj::Scene scene;
    Player* player = PlacePressScene(scene, false, 8.0f);
    ASSERT_NE(player, nullptr);
    player->Update(false);
    ASSERT_FALSE(player->Body().IsGrounded());
    player->RequestBodySlam(0.0f, k_Forward);
    int startsInTheAir = 0;
    bool slamEnded = false;
    bool pressedInTheAir = false;
    for (int frame = 0; frame < 180 && !player->Body().IsGrounded(); ++frame)
    {
        if (StepAndSeeSlamStart(*player, nullptr) && !player->Body().IsGrounded())
        {
            ++startsInTheAir;
        }
        if (startsInTheAir == 1 && !player->IsBodySlamming())
        {
            slamEnded = true;
        }
        // 終わった後の空中で押し続ける
        if (slamEnded && !player->Body().IsGrounded())
        {
            player->RequestBodySlam(0.0f, k_Forward);
            pressedInTheAir = true;
        }
    }
    EXPECT_TRUE(slamEnded);
    EXPECT_TRUE(pressedInTheAir);
    EXPECT_EQ(startsInTheAir, 1);
}
