#include "Game/Level/ImpactResolver.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "Game/Player/PlayerAppearance.h"
#include "Game/Player/PlayerParams.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Object/Components/TransformComponent.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

// 玉の回転を、届くまで (反動は着地まで) の回転数で持つ
// 速さや距離を触っても回転数は変わらない。溜めた突進と通常突進、溜めて当てた反動と通常突進で当てた反動で数を変える

namespace
{
    constexpr float k_FrameSeconds = 1.0f / 60.0f;

    // 自機 (id 1) を置く。withRock なら前に置物 (id 2) を置く
    Player* PlaceSpinScene(NS::Obj::Scene& scene, bool withRock)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json player = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(player, "Player");
        NS::Obj::SetObjectJsonId(player, 1);
        NS::Obj::SetObjectPosition(player, NS::Vector3{0.0f, 1.0f, 0.0f});
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
        PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
        return NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    }

    float FieldOf(Player& player, const char* name)
    {
        return NS::Obj::SerializeComponentFields(player.Params())[name].get<float>();
    }

    // 突進を出して、突進の間に回った角度の和 (度) を返す
    float SpinWhileSlamming(Player& player, float charge01)
    {
        player.RequestBodySlam(charge01, NS::Vector3{0.0f, 0.0f, 1.0f});
        float degrees = 0.0f;
        bool started = false;
        for (int frame = 0; frame < 240; ++frame)
        {
            player.Update(false);
            if (player.IsBodySlamming())
            {
                started = true;
                degrees += player.Appearance().SpinDegreesThisFrame();
            }
            else if (started)
            {
                break;
            }
        }
        return degrees;
    }

    // 置物に当てて、反動の始まりから着地までに回った角度の和 (度) を返す。反動しなければ負
    float SpinWhileRebounding(Player& player, NS::Game::Level::MapObj& rock, float charge01)
    {
        player.RequestBodySlam(charge01, NS::Vector3{0.0f, 0.0f, 1.0f});
        float degrees = 0.0f;
        bool started = false;
        for (int frame = 0; frame < 400; ++frame)
        {
            player.Update(false);
            rock.Update();
            if (player.IsRebounding())
            {
                started = true;
                degrees += player.Appearance().SpinDegreesThisFrame();
            }
            else if (started)
            {
                return degrees;
            }
        }
        return -1.0f;
    }
} // namespace

// 通常突進の初速は欄の値
TEST(PlayerSpinCount, TapLeavesAtItsFieldSpeed)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSpinScene(scene, false);
    ASSERT_NE(player, nullptr);
    EXPECT_FLOAT_EQ(FieldOf(*player, "通常突進の初速"), 15.0f);
    player->RequestBodySlam(0.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
    player->Update(false);
    ASSERT_TRUE(player->IsBodySlamming());
    const NS::Vector3 velocity = player->BodySlamVelocity();
    EXPECT_NEAR(std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z), 15.0f, 1.0e-3f);
}

// 通常突進は届くまでに欄の回転数だけ回る。速さと距離を変えても同じ
TEST(PlayerSpinCount, TapTurnsItsCountWhateverTheSpeedAndDistance)
{
    for (const float speed : {15.0f, 10.0f})
    {
        for (const float distance : {6.25f, 4.0f})
        {
            SCOPED_TRACE(std::to_string(speed) + " " + std::to_string(distance));
            NS::Obj::Scene scene;
            Player* player = PlaceSpinScene(scene, false);
            ASSERT_NE(player, nullptr);
            NS::Obj::ApplyJsonFields(player->Params(),
                                     nlohmann::json{{"通常突進の初速", speed}, {"通常突進の距離", distance}});
            const float turns = FieldOf(*player, "通常突進の届くまでの回転数");
            const float perFrame = turns * 360.0f * speed / distance * k_FrameSeconds;
            EXPECT_NEAR(SpinWhileSlamming(*player, 0.0f), turns * 360.0f, perFrame * 1.01f);
        }
    }
}

// 溜めた突進は溜めた突進の回転数で回り、通常突進とはっきり違う
TEST(PlayerSpinCount, ChargedSlamTurnsItsOwnCount)
{
    NS::Obj::Scene scene;
    Player* player = PlaceSpinScene(scene, false);
    ASSERT_NE(player, nullptr);
    const float turns = FieldOf(*player, "溜めた突進の届くまでの回転数");
    EXPECT_GE(turns - FieldOf(*player, "通常突進の届くまでの回転数"), 2.0f);
    const float perFrame =
        turns * 360.0f * FieldOf(*player, "チャージ突進の速度") / FieldOf(*player, "チャージ突進の距離") * k_FrameSeconds;
    EXPECT_NEAR(SpinWhileSlamming(*player, 1.0f), turns * 360.0f, perFrame * 1.01f);
}

// 反動は着地までに、溜めて当てた時と通常突進で当てた時のそれぞれの回転数だけ回る
TEST(PlayerSpinCount, ReboundTurnsTheCountOfTheSlamThatHit)
{
    for (const float charge01 : {1.0f, 0.0f})
    {
        SCOPED_TRACE(charge01);
        NS::Obj::Scene scene;
        Player* player = PlaceSpinScene(scene, true);
        ASSERT_NE(player, nullptr);
        NS::Game::Level::MapObj* rock = NS::Obj::Cast<NS::Game::Level::MapObj>(scene.Objects().FindByObjectId(2));
        ASSERT_NE(rock, nullptr);
        const char* field = "溜めて当てた反動の回転数";
        if (charge01 <= 0.0f)
        {
            field = "通常突進で当てた反動の回転数";
        }
        const float turns = FieldOf(*player, field);
        const float degrees = SpinWhileRebounding(*player, *rock, charge01);
        ASSERT_GT(degrees, 0.0f);
        // 着地のフレームの揃い方で 1〜2 フレームぶんずれる
        EXPECT_NEAR(degrees, turns * 360.0f, turns * 360.0f * 0.1f);
    }
}
