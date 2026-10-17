#include "Game/Level/LaunchArc.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/MapObjParams.h"
#include "Game/Level/PropActor.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Windows/FileSystem.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

TEST(MapObjParams, SceneOverrideControlsMassAndToughness)
{
    NS::Scene scene;
    nlohmann::json doc = NS::MakeSceneJson();
    nlohmann::json rock = NS::MakeObjectJson();
    NS::SetObjectJsonClass(rock, "PropActor");
    NS::SetObjectJsonId(rock, 1);
    rock["classObjs"] = {{"Params", {{"質量", 2.0f}, {"耐久", 3.0f}}}};
    NS::SceneJsonObjects(doc).push_back(std::move(rock));
    scene.ResetFromJson(doc);
    NS::SceneActor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    const GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(actor);
    ASSERT_NE(prop, nullptr);
    const GL::Level::MapObjParams* params = &prop->Params();
    EXPECT_FLOAT_EQ(params->Mass(), 2.0f);
    EXPECT_FLOAT_EQ(params->Toughness(), 3.0f);
    EXPECT_EQ(NS::ClassObjFields(NS::MakePrototypeJson(*actor), "Breakable"), nullptr);
    GL::Level::TackleTargetAnswer answer;
    ASSERT_TRUE(GL::Level::SendMsgAskTackleTarget(*actor->BodySensorClassObj(), answer));
    EXPECT_FLOAT_EQ(answer.mass, 2.0f);
    EXPECT_FLOAT_EQ(answer.toughness, 3.0f);
}

TEST(MapObjParams, HitTimelineNameIsEmptyByDefaultAndReachesTheAnswer)
{
    NS::Scene scene;
    nlohmann::json doc = NS::MakeSceneJson();
    for (const NS::uint32 id : {1u, 2u})
    {
        nlohmann::json rock = NS::MakeObjectJson();
        NS::SetObjectJsonClass(rock, "PropActor");
        NS::SetObjectJsonId(rock, id);
        NS::SetObjectPosition(rock, NS::Vector3{static_cast<float>(id) * 5.0f, 0.5f, 0.0f});
        if (id == 2)
        {
            rock["classObjs"] = {{"Params", {{"当たりのタイムライン", "barrel"}}}};
        }
        NS::SceneJsonObjects(doc).push_back(std::move(rock));
    }
    scene.ResetFromJson(doc);
    for (const NS::uint32 id : {1u, 2u})
    {
        SCOPED_TRACE(id);
        std::string_view expected;
        if (id == 2)
        {
            expected = "barrel";
        }
        NS::SceneActor* actor = scene.Objects().FindByObjectId(id);
        ASSERT_NE(actor, nullptr);
        const GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(actor);
        ASSERT_NE(prop, nullptr);
        const GL::Level::MapObjParams* params = &prop->Params();
        EXPECT_EQ(params->HitTimelineName(), expected);
        GL::Level::TackleTargetAnswer answer;
        ASSERT_TRUE(GL::Level::SendMsgAskTackleTarget(*actor->BodySensorClassObj(), answer));
        EXPECT_EQ(answer.hitTimeline, expected);
    }
}

TEST(MapObjParams, ShippedScenesKeepIndividualMassAndToughness)
{
    for (const std::string_view sceneName : {"new_scene.scene"})
    {
        const std::string path = NS::OS::FileSystem::Combine(
            NS::OS::FileSystem::Combine(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"),
                                        "Scenes"),
            sceneName);
        nlohmann::json doc;
        ASSERT_TRUE(NS::LoadSceneFromJsonFile(doc, path));
        NS::Scene scene;
        scene.ResetFromJson(doc);
        std::vector<float> masses;
        std::vector<float> toughnesses;
        for (const nlohmann::json& entry : NS::SceneJsonObjects(doc))
        {
            if (NS::ObjectJsonClass(entry) != "PropActor")
            {
                continue;
            }
            const NS::SceneActor* rock = scene.Objects().FindByObjectId(NS::ObjectJsonId(entry));
            ASSERT_NE(rock, nullptr);
            const GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(rock);
            ASSERT_NE(prop, nullptr);
            masses.push_back(prop->Params().Mass());
            toughnesses.push_back(prop->Params().Toughness());
        }
        std::sort(masses.begin(), masses.end());
        std::sort(toughnesses.begin(), toughnesses.end());
        EXPECT_EQ(masses, (std::vector<float>{0.5f, 1.0f, 2.0f, 8.0f}));
        EXPECT_EQ(toughnesses, (std::vector<float>{0.5f, 1.0f, 2.0f, 5.0f}));
    }
}

TEST(MapObjParams, InvalidOverridesKeepPhysicalValuesSafe)
{
    NS::Scene scene;
    nlohmann::json doc = NS::MakeSceneJson();
    nlohmann::json rock = NS::MakeObjectJson();
    NS::SetObjectJsonClass(rock, "PropActor");
    NS::SetObjectJsonId(rock, 1);
    rock["classObjs"] = {{"Params", {{"質量", -2.0f}, {"耐久", -3.0f}, {"摩擦", -1.0f}, {"跳ね返り", -1.0f}}}};
    NS::SceneJsonObjects(doc).push_back(std::move(rock));
    scene.ResetFromJson(doc);
    const NS::SceneActor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    const GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(actor);
    ASSERT_NE(prop, nullptr);
    const GL::Level::MapObjParams* params = &prop->Params();
    EXPECT_FLOAT_EQ(params->Mass(), 1.0f);
    EXPECT_FLOAT_EQ(params->Toughness(), 0.0f);
    EXPECT_FLOAT_EQ(params->Friction(), 0.0f);
    EXPECT_FLOAT_EQ(params->Restitution(), 0.0f);
}

// 飛び方の欄の表示名と既定。表示名は保存の鍵になる
TEST(MapObjParams, LaunchShapeDefaultsBelongToParams)
{
    GL::Level::MapObjParams params;
    const nlohmann::json fields = NS::SerializeClassObjFields(params);
    const nlohmann::json expected = {{"押し飛ばしの高さ", 2.0f},
                                     {"押し飛ばしの上昇重力", 25.0f},
                                     {"下りの速さの倍率", 1.4f},
                                     {"頂点の帯の縦速度", 1.0f},
                                     {"頂点の帯の重力倍率", 0.5f},
                                     {"外れで飛ばす相手の弧の高さの割合", 0.35f},
                                     {"外れで飛ばす相手の距離の割合", 0.1f}};
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(fields.contains(it.key())) << it.key();
        EXPECT_EQ(fields[it.key()], it.value()) << it.key();
    }
    const GL::Level::LaunchShape shape = params.Launch();
    EXPECT_FLOAT_EQ(shape.apexHeight, 2.0f);
    EXPECT_FLOAT_EQ(shape.riseGravity, 25.0f);
    EXPECT_FLOAT_EQ(shape.fallGravityScale, 1.4f);
    EXPECT_FLOAT_EQ(shape.apexBandSpeed, 1.0f);
    EXPECT_FLOAT_EQ(shape.apexBandGravityScale, 0.5f);
    EXPECT_FLOAT_EQ(shape.missHeightRatio, 0.35f);
    EXPECT_FLOAT_EQ(shape.missDistanceRatio, 0.1f);
}

TEST(MapObjParams, TackleAnswerCarriesTheLaunchShape)
{
    NS::Scene scene;
    nlohmann::json doc = NS::MakeSceneJson();
    nlohmann::json rock = NS::MakeObjectJson();
    NS::SetObjectJsonClass(rock, "PropActor");
    NS::SetObjectJsonId(rock, 1);
    rock["classObjs"] = {{"Params", {{"押し飛ばしの高さ", 3.0f}, {"外れで飛ばす相手の距離の割合", 0.5f}}}};
    NS::SceneJsonObjects(doc).push_back(std::move(rock));
    scene.ResetFromJson(doc);
    NS::SceneActor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    GL::Level::TackleTargetAnswer answer;
    ASSERT_TRUE(GL::Level::SendMsgAskTackleTarget(*actor->BodySensorClassObj(), answer));
    EXPECT_FLOAT_EQ(answer.launch.apexHeight, 3.0f);
    EXPECT_FLOAT_EQ(answer.launch.missDistanceRatio, 0.5f);
    EXPECT_FLOAT_EQ(answer.launch.riseGravity, 25.0f);
}

// 尾と粉の大きさは描く LaunchEffects の欄。遊びの欄と並ばない
TEST(MapObjParams, LaunchSizeDefaultsBelongToLaunchEffects)
{
    GL::Level::LaunchEffects effects{nullptr};
    const nlohmann::json fields = NS::SerializeClassObjFields(effects);
    const nlohmann::json expected = {{"飛び出しの尾が残るフレーム数の基準", 8},
                                     {"飛び出しの尾が残るフレーム数の飛ばしの比あたり", 4.0f},
                                     {"着地の粉の大きさの基準", 0.8f},
                                     {"着地の粉の大きさの質量の平方根あたり", 0.4f},
                                     {"着地の粉の大きさの威力あたりの伸び", 0.5f}};
    GL::Level::MapObjParams params;
    const nlohmann::json paramFields = NS::SerializeClassObjFields(params);
    for (nlohmann::json::const_iterator it = expected.begin(); it != expected.end(); ++it)
    {
        ASSERT_TRUE(fields.contains(it.key())) << it.key();
        EXPECT_EQ(fields[it.key()], it.value()) << it.key();
        EXPECT_FALSE(paramFields.contains(it.key())) << it.key();
    }
}

TEST(MapObjParams, LiveLaunchVisualTuningControlsTrailAndDust)
{
    NS::Scene scene;
    nlohmann::json doc = NS::MakeSceneJson();
    nlohmann::json rock = NS::MakeObjectJson();
    NS::SetObjectJsonClass(rock, "PropActor");
    NS::SetObjectJsonId(rock, 1);
    NS::SceneJsonObjects(doc).push_back(std::move(rock));
    scene.ResetFromJson(doc);
    NS::SceneActor* actor = scene.Objects().FindByObjectId(1);
    ASSERT_NE(actor, nullptr);
    GL::Level::PropActor* placed = NS::Cast<GL::Level::PropActor>(actor);
    ASSERT_NE(placed, nullptr);
    GL::Level::MapObjParams* params = &placed->Params();
    GL::Level::LaunchEffects* effects = &placed->Effects();
    EXPECT_EQ(NS::ApplyJsonFields(*params, {{"質量", 4.0f}}), 0u);
    EXPECT_EQ(NS::ApplyJsonFields(*effects,
                                  {{"飛び出しの尾が残るフレーム数の基準", 13},
                                   {"飛び出しの尾が残るフレーム数の飛ばしの比あたり", 7.0f},
                                   {"着地の粉の大きさの基準", 1.0f},
                                   {"着地の粉の大きさの質量の平方根あたり", 0.5f},
                                   {"着地の粉の大きさの威力あたりの伸び", 0.25f}}),
              0u);
    GL::Level::TackleReleaseDesc release{};
    release.arc = GL::Level::LaunchArc{.distance = 5.0f, .apexHeight = 1.0f};
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(*actor, release));
    GL::Level::TackleReleaseDesc trail = release;
    trail.tier = GL::Level::HitTier::Center;
    trail.power = 2.0f;
    trail.launchScale = 1.0f;
    const GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(actor);
    const NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(actor->CollisionClassObj());
    ASSERT_NE(prop, nullptr);
    ASSERT_NE(sphere, nullptr);
    effects->BeginTrail(trail, prop->Flight(), params, sphere);
    EXPECT_EQ(effects->TrailFrames(), 20);
    EXPECT_FLOAT_EQ(effects->LandDustScale(), 2.5f);
}
