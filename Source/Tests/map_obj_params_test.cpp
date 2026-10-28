#include "Editor/EditorActors.h"
#include "Game/Level/LaunchArc.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/MapObjParams.h"
#include "Game/Level/PropActor.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Core/Windows/FileSystem.h"
#include "TestActorFile.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <vector>

namespace
{
    // PropActor を元にして、Params に fields を書いた派生の .actor の文書
    nlohmann::json DerivedPropActorFile(const nlohmann::json& fields)
    {
        nlohmann::json actorFile = ClassOnlyActorFile("PropActor");
        NS::SetActorFileBase(actorFile, "PropActor");
        NS::ActorJsonClassObjs(actorFile)["Params"] = fields;
        return actorFile;
    }

    // actorFile の .actor を指す置物 1 体を、1 番に置いたシーンへ組み直す
    void ResetOneRock(NS::Scene& scene, std::string_view actorFile, const nlohmann::json& classObjs)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json rock = NS::MakeActorJson();
        NS::SetActorJsonFile(rock, actorFile);
        NS::SetActorJsonId(rock, 1);
        rock["classObjs"] = classObjs;
        NS::SceneJsonActors(doc).push_back(std::move(rock));
        scene.ResetFromJson(doc);
    }
} // namespace

// 質量と耐久はプロテクトの項目で、派生の .actor が決める。シーンでは書けない
TEST(MapObjParams, DerivedActorFileControlsMassAndToughness)
{
    const NS::FieldDesc* mass = NS::FindField(GL::Level::MapObjParams::StaticReflection(), "質量");
    const NS::FieldDesc* toughness = NS::FindField(GL::Level::MapObjParams::StaticReflection(), "耐久");
    ASSERT_NE(mass, nullptr);
    ASSERT_NE(toughness, nullptr);
    EXPECT_EQ(mass->scope, NS::FieldScope::Protected);
    EXPECT_EQ(toughness->scope, NS::FieldScope::Protected);

    const ScopedActorFile heavy{"HeavyRock", DerivedPropActorFile({{"質量", 2.0f}, {"耐久", 3.0f}})};
    NS::Scene scene;
    ResetOneRock(scene, "HeavyRock", nlohmann::json::object());
    NS::SceneActor* actor = scene.Actors().FindByActorId(1);
    ASSERT_NE(actor, nullptr);
    const GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(actor);
    ASSERT_NE(prop, nullptr);
    const GL::Level::MapObjParams* params = &prop->Params();
    EXPECT_FLOAT_EQ(params->Mass(), 2.0f);
    EXPECT_FLOAT_EQ(params->Toughness(), 3.0f);
    EXPECT_EQ(NS::ClassObjFields(NS::Editor::MakePrototypeJson(actor), "Breakable"), nullptr);
    GL::Level::TackleTargetAnswer answer;
    ASSERT_TRUE(GL::Level::SendMsgAskTackleTarget(actor->HitSensorsObjClassObj()->Find("Body"), answer));
    EXPECT_FLOAT_EQ(answer.mass, 2.0f);
    EXPECT_FLOAT_EQ(answer.toughness, 3.0f);

    // シーンに書いた質量は、書いてよくない項目なので ERROR を出してアクターを作らない
    const TestLogCapture log;
    ResetOneRock(scene, "PropActor", {{"Params", {{"質量", 2.0f}}}});
    EXPECT_EQ(scene.Actors().FindByActorId(1), nullptr);
    EXPECT_GE(log.ErrorCount(), 1);
}

// 出荷シーンの置物は、質量と耐久を派生の .actor から受け取り、シーンには書かない。値は派生へ移す前と同じ
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
        struct Expected
        {
            NS::uint32 id;
            float mass;
            float toughness;
        };
        for (const Expected& expected : {Expected{35, 0.5f, 0.5f}, Expected{41, 1.0f, 1.0f},
                                         Expected{47, 2.0f, 2.0f}, Expected{53, 8.0f, 5.0f}})
        {
            SCOPED_TRACE(expected.id);
            const GL::Level::PropActor* prop =
                NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(expected.id));
            ASSERT_NE(prop, nullptr);
            EXPECT_FLOAT_EQ(prop->Params().Mass(), expected.mass);
            EXPECT_FLOAT_EQ(prop->Params().Toughness(), expected.toughness);
            const nlohmann::json& entry = NS::SceneJsonActors(doc)[NS::FindActorIndexById(doc, expected.id)];
            const nlohmann::json* params = NS::ClassObjFields(entry, "Params");
            EXPECT_TRUE(params == nullptr || params->empty());
        }
    }
}

TEST(MapObjParams, InvalidOverridesKeepPhysicalValuesSafe)
{
    const ScopedActorFile broken{"BrokenRock", DerivedPropActorFile({{"質量", -2.0f}, {"耐久", -3.0f}})};
    NS::Scene scene;
    ResetOneRock(scene, "BrokenRock", nlohmann::json::object());
    GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(1));
    ASSERT_NE(prop, nullptr);
    // 摩擦と跳ね返りは .actor だけが持つ欄なので、読んだ後のアクターへ書く
    ASSERT_EQ(NS::ApplyJsonFields(&prop->Params(), nlohmann::json{{"摩擦", -1.0f}, {"跳ね返り", -1.0f}}), 0u);
    const GL::Level::MapObjParams* params = &prop->Params();
    EXPECT_FLOAT_EQ(params->Mass(), 1.0f);
    EXPECT_FLOAT_EQ(params->Toughness(), 0.0f);
    EXPECT_FLOAT_EQ(params->Friction(), 0.0f);
    EXPECT_FLOAT_EQ(params->Restitution(), 0.0f);
}

// 質量は正の数だけを書く。0・負・無限大を読むと今の値を残す。Mass() の読み替えでなく欄の値で見る
TEST(MapObjParams, MassFieldKeepsTheValueOnZeroNegativeAndInfinite)
{
    GL::Level::MapObjParams params;
    const NS::FieldDesc* field = NS::FindField(params.GetReflection(), "質量");
    ASSERT_NE(field, nullptr);
    float before = 0.0f;
    field->get(&params, &before);
    // 1e39 は float に収まらず、読むと無限大になる
    for (const double value : {0.0, -2.0, 1.0e39})
    {
        SCOPED_TRACE(value);
        EXPECT_EQ(NS::ApplyJsonFields(&params, nlohmann::json{{"質量", value}}), 0u);
        float after = 0.0f;
        field->get(&params, &after);
        EXPECT_EQ(after, before);
    }
}

// 飛び方の欄の表示名と既定。表示名は保存の鍵になる
TEST(MapObjParams, LaunchShapeDefaultsBelongToParams)
{
    GL::Level::MapObjParams params;
    const nlohmann::json fields = NS::SerializeClassObjFields(&params);
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
    nlohmann::json rock = NS::MakeActorJson();
    NS::SetActorJsonFile(rock, "PropActor");
    NS::SetActorJsonId(rock, 1);
    NS::SceneJsonActors(doc).push_back(std::move(rock));
    scene.ResetFromJson(doc);
    GL::Level::PropActor* actor = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(1));
    ASSERT_NE(actor, nullptr);
    // 飛び方の欄は .actor だけが持つ欄なので、読んだ後のアクターへ書く
    ASSERT_EQ(NS::ApplyJsonFields(&actor->Params(),
                                  nlohmann::json{{"押し飛ばしの高さ", 3.0f}, {"外れで飛ばす相手の距離の割合", 0.5f}}),
              0u);
    GL::Level::TackleTargetAnswer answer;
    ASSERT_TRUE(GL::Level::SendMsgAskTackleTarget(actor->HitSensorsObjClassObj()->Find("Body"), answer));
    EXPECT_FLOAT_EQ(answer.launch.apexHeight, 3.0f);
    EXPECT_FLOAT_EQ(answer.launch.missDistanceRatio, 0.5f);
    EXPECT_FLOAT_EQ(answer.launch.riseGravity, 25.0f);
}

// 尾と粉の大きさは描く LaunchEffects の欄。遊びの欄と並ばない
TEST(MapObjParams, LaunchSizeDefaultsBelongToLaunchEffects)
{
    GL::Level::LaunchEffects effects;
    const nlohmann::json fields = NS::SerializeClassObjFields(&effects);
    const nlohmann::json expected = {{"飛び出しの尾が残るフレーム数の基準", 8},
                                     {"飛び出しの尾が残るフレーム数の飛ばしの比あたり", 4.0f},
                                     {"着地の粉の大きさの基準", 0.8f},
                                     {"着地の粉の大きさの質量の平方根あたり", 0.4f},
                                     {"着地の粉の大きさの威力あたりの伸び", 0.5f}};
    GL::Level::MapObjParams params;
    const nlohmann::json paramFields = NS::SerializeClassObjFields(&params);
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
    nlohmann::json rock = NS::MakeActorJson();
    NS::SetActorJsonFile(rock, "PropActor");
    NS::SetActorJsonId(rock, 1);
    NS::SceneJsonActors(doc).push_back(std::move(rock));
    scene.ResetFromJson(doc);
    NS::SceneActor* actor = scene.Actors().FindByActorId(1);
    ASSERT_NE(actor, nullptr);
    GL::Level::PropActor* placed = NS::Cast<GL::Level::PropActor>(actor);
    ASSERT_NE(placed, nullptr);
    GL::Level::MapObjParams* params = &placed->Params();
    GL::Level::LaunchEffects* effects = &placed->Effects();
    EXPECT_EQ(NS::ApplyJsonFields(params, {{"質量", 4.0f}}), 0u);
    EXPECT_EQ(NS::ApplyJsonFields(effects,
                                  {{"飛び出しの尾が残るフレーム数の基準", 13},
                                   {"飛び出しの尾が残るフレーム数の飛ばしの比あたり", 7.0f},
                                   {"着地の粉の大きさの基準", 1.0f},
                                   {"着地の粉の大きさの質量の平方根あたり", 0.5f},
                                   {"着地の粉の大きさの威力あたりの伸び", 0.25f}}),
              0u);
    GL::Level::TackleReleaseDesc release{};
    release.arc = GL::Level::LaunchArc{.distance = 5.0f, .apexHeight = 1.0f};
    ASSERT_TRUE(GL::Level::SendMsgTackleRelease(actor, release));
    GL::Level::TackleReleaseDesc trail = release;
    trail.tier = GL::Level::HitTier::Center;
    trail.power = 2.0f;
    trail.launchScale = 1.0f;
    const GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(actor);
    const NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(actor->CollisionClassObj());
    ASSERT_NE(prop, nullptr);
    ASSERT_NE(sphere, nullptr);
    effects->BeginTrail(
        prop, trail,prop->Flight(), params, sphere->WorldSphere(prop->Root().WorldMatrix()));
    EXPECT_EQ(effects->TrailFrames(), 20);
    EXPECT_FLOAT_EQ(effects->LandDustScale(), 2.5f);
}
