#include "Game/Level/LaunchArc.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/MapObjParams.h"
#include "NSlib/Object/Components/HitSensor.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/Windows/FileSystem.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

TEST(MapObjParams, SceneOverrideControlsMassAndToughness)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json rock = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(rock, "MapObj");
    NS::Obj::SetObjectJsonId(rock, 1);
    rock["parts"] = {{"Params", {{"質量", 2.0f}, {"耐久", 3.0f}}}};
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
    scene.LoadJson(doc);
    NS::Obj::Actor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    const NS::Game::Level::MapObjParams* params =
        NS::Obj::ComponentCast<NS::Game::Level::MapObjParams>(actor->Part("Params"));
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->Mass(), 2.0f);
    EXPECT_FLOAT_EQ(params->Toughness(), 3.0f);
    EXPECT_EQ(actor->Part("Breakable"), nullptr);
    NS::Game::Level::TackleTargetAnswer answer;
    ASSERT_TRUE(NS::Game::Level::SendMsgAskTackleTarget(*actor->BodySensorPart(), answer));
    EXPECT_FLOAT_EQ(answer.mass, 2.0f);
    EXPECT_FLOAT_EQ(answer.toughness, 3.0f);
}

TEST(MapObjParams, ShippedScenesKeepIndividualMassAndToughness)
{
    for (const std::string_view sceneName : {"new_scene.scene", "course.scene"})
    {
        const std::string path = NS::OS::FileSystem::Combine(
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"),
                                        "Scenes"),
            sceneName);
        nlohmann::json doc;
        ASSERT_TRUE(NS::Obj::LoadSceneFromJsonFile(doc, path));
        NS::Obj::Scene scene;
        scene.LoadJson(doc);
        std::vector<float> masses;
        std::vector<float> toughnesses;
        for (const nlohmann::json& entry : NS::Obj::SceneJsonObjects(doc))
        {
            if (NS::Obj::ObjectJsonClass(entry) != "MapObj")
            {
                continue;
            }
            const NS::Obj::Actor* rock = scene.Objects().FindByObjectId(NS::Obj::ObjectJsonId(entry));
            ASSERT_NE(rock, nullptr);
            const NS::Game::Level::MapObjParams* params =
                NS::Obj::ComponentCast<NS::Game::Level::MapObjParams>(rock->Part("Params"));
            ASSERT_NE(params, nullptr);
            masses.push_back(params->Mass());
            toughnesses.push_back(params->Toughness());
        }
        std::sort(masses.begin(), masses.end());
        std::sort(toughnesses.begin(), toughnesses.end());
        if (sceneName == "new_scene.scene")
        {
            EXPECT_EQ(masses, (std::vector<float>{0.5f, 1.0f, 2.0f, 8.0f}));
            EXPECT_EQ(toughnesses, (std::vector<float>{0.5f, 1.0f, 2.0f, 5.0f}));
        }
        else
        {
            EXPECT_EQ(masses, (std::vector<float>{2.0f, 8.0f, 8.0f}));
            EXPECT_EQ(toughnesses, (std::vector<float>{2.0f, 5.0f, 5.0f}));
        }
    }
}

TEST(MapObjParams, InvalidOverridesKeepPhysicalValuesSafe)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json rock = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(rock, "MapObj");
    NS::Obj::SetObjectJsonId(rock, 1);
    rock["parts"] = {{"Params", {{"質量", -2.0f}, {"耐久", -3.0f}, {"摩擦", -1.0f}, {"跳ね返り", -1.0f}}}};
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
    scene.LoadJson(doc);
    const NS::Obj::Actor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    const NS::Game::Level::MapObjParams* params =
        NS::Obj::ComponentCast<NS::Game::Level::MapObjParams>(actor->Part("Params"));
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->Mass(), 1.0f);
    EXPECT_FLOAT_EQ(params->Toughness(), 0.0f);
    EXPECT_FLOAT_EQ(params->Friction(), 0.0f);
    EXPECT_FLOAT_EQ(params->Restitution(), 0.0f);
}

TEST(MapObjParams, LaunchSizeDefaultsBelongToParams)
{
    NS::Game::Level::MapObjParams params;
    const nlohmann::json fields = NS::Obj::SerializeComponent(params)["fields"];
    const nlohmann::json expected = {{"飛び出しの尾が残るフレーム数の基準", 8},
                                     {"飛び出しの尾が残るフレーム数の飛ばしの比あたり", 4.0f},
                                     {"着地の粉の大きさの基準", 0.8f},
                                     {"着地の粉の大きさの質量の平方根あたり", 0.4f},
                                     {"着地の粉の大きさの威力あたりの伸び", 0.5f}};
    NS::Game::Level::LaunchEffects effects;
    const nlohmann::json effectFields = NS::Obj::SerializeComponent(effects)["fields"];
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(fields.contains(it.key())) << it.key();
        EXPECT_EQ(fields[it.key()], it.value()) << it.key();
        EXPECT_FALSE(effectFields.contains(it.key())) << it.key();
    }
}

TEST(MapObjParams, LiveLaunchVisualTuningControlsTrailAndDust)
{
    NS::Obj::Scene scene;
    nlohmann::json doc = NS::Obj::MakeSceneJson();
    nlohmann::json rock = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(rock, "MapObj");
    NS::Obj::SetObjectJsonId(rock, 1);
    NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
    scene.LoadJson(doc);
    NS::Obj::Actor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    NS::Game::Level::MapObjParams* params =
        NS::Obj::ComponentCast<NS::Game::Level::MapObjParams>(actor->Part("Params"));
    NS::Game::Level::LaunchEffects* effects =
        NS::Obj::ComponentCast<NS::Game::Level::LaunchEffects>(actor->Part("LaunchEffects"));
    ASSERT_NE(params, nullptr);
    ASSERT_NE(effects, nullptr);
    EXPECT_EQ(NS::Obj::ApplyJsonFields(*params,
                                       {{"質量", 4.0f},
                                        {"飛び出しの尾が残るフレーム数の基準", 13},
                                        {"飛び出しの尾が残るフレーム数の飛ばしの比あたり", 7.0f},
                                        {"着地の粉の大きさの基準", 1.0f},
                                        {"着地の粉の大きさの質量の平方根あたり", 0.5f},
                                        {"着地の粉の大きさの威力あたりの伸び", 0.25f}}),
              0u);
    NS::Game::Level::TackleReleaseDesc release{};
    release.arc = NS::Game::Level::LaunchArc{.distance = 5.0f, .apexHeight = 1.0f};
    ASSERT_TRUE(NS::Game::Level::SendMsgTackleRelease(*actor, release));
    effects->BeginTrail(NS::Game::Level::HitTier::Center, 2.0f, 1.0f, NS::Vector3{1.0f, 0.0f, 0.0f});
    EXPECT_EQ(effects->TrailFrames(), 20);
    EXPECT_FLOAT_EQ(effects->LandDustScale(), 2.5f);
}
