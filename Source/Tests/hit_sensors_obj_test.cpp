#include "Game/Level/DeathZoneActor.h"
#include "Game/Level/GoalActor.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/HitSensorsObj.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"
#include "TestActorFile.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// センサーをまとめて持つ部品が、名前つきのセンサーを作って管理クラスへ入れる事と、.actor の hitSensors の読み書きを縛る

namespace
{
    using GL::Level::SensorKind;
    using NS::Vector3;

    // センサーを宣言しない試しの SceneActor。部品の親にだけ使う
    class SensorsOwnerActor final : public NS::SceneActor
    {
    };

    [[nodiscard]] std::size_t ListedCount(const NS::Scene& scene, const NS::HitSensor* sensor)
    {
        const std::vector<NS::HitSensor*>& listed = scene.HitSensors().Sensors();
        return static_cast<std::size_t>(std::count(listed.begin(), listed.end(), sensor));
    }

    // 今のゴールの .actor の体のセンサーの値
    [[nodiscard]] nlohmann::json GoalBodyValues()
    {
        return nlohmann::json{{"中心オフセット", {0.0, 0.3, 0.0}},
                              {"半分の高さ", 0.5},
                              {"半径", 0.85},
                              {"箱の半径", {0.5, 0.5, 0.5}}};
    }

    // 今の落下の区域の .actor の体のセンサーの値
    [[nodiscard]] nlohmann::json DeathZoneBodyValues()
    {
        return nlohmann::json{{"中心オフセット", {0.0, 0.0, 0.0}},
                              {"半分の高さ", 0.5},
                              {"半径", 0.5},
                              {"箱の半径", {1000.0, 5.0, 1000.0}}};
    }

    // sensor の大きさが values (.actor の 1 つのセンサーの値) と同じか
    void ExpectSizes(const NS::ShapeHitSensor& sensor, const nlohmann::json& values)
    {
        EXPECT_FLOAT_EQ(sensor.Radius(), values["半径"].get<float>());
        EXPECT_FLOAT_EQ(sensor.HalfHeight(), values["半分の高さ"].get<float>());
        EXPECT_FLOAT_EQ(sensor.CenterOffset().x, values["中心オフセット"][0].get<float>());
        EXPECT_FLOAT_EQ(sensor.CenterOffset().y, values["中心オフセット"][1].get<float>());
        EXPECT_FLOAT_EQ(sensor.CenterOffset().z, values["中心オフセット"][2].get<float>());
        EXPECT_FLOAT_EQ(sensor.BoxHalfExtents().x, values["箱の半径"][0].get<float>());
        EXPECT_FLOAT_EQ(sensor.BoxHalfExtents().y, values["箱の半径"][1].get<float>());
        EXPECT_FLOAT_EQ(sensor.BoxHalfExtents().z, values["箱の半径"][2].get<float>());
    }
} // namespace

// 作ったセンサーは名前・種類・形・親を持ち、管理クラスへ 1 回だけ入る
TEST(HitSensorsObj, AddShapeMakesANamedSensorListedOnce)
{
    NS::Scene scene;
    SensorsOwnerActor* owner = scene.SpawnActor<SensorsOwnerActor>("SensorsOwnerActor");
    ASSERT_NE(owner, nullptr);
    NS::HitSensorsObj sensors;

    NS::ShapeHitSensor* body = sensors.AddShape(owner, "Body", SensorKind::Area, NS::HitSensorShape::Box);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->Name(), "Body");
    EXPECT_TRUE(GL::Level::IsSensorKind(body, SensorKind::Area));
    EXPECT_EQ(body->Shape(), NS::HitSensorShape::Box);
    EXPECT_EQ(body->Host(), owner);
    EXPECT_EQ(ListedCount(scene, body), 1u);
    EXPECT_EQ(sensors.Find("Body"), body);
    EXPECT_EQ(sensors.Find("Attack"), nullptr);
    EXPECT_EQ(sensors.Sensors(), (std::vector<NS::HitSensor*>{body}));
    sensors.EndAll(owner);
}

// 形を毎回読むセンサーも、名前・種類・親を持ち、受けた関数の形を返す
TEST(HitSensorsObj, AddFollowMakesANamedSensorThatReadsTheVolume)
{
    NS::Scene scene;
    SensorsOwnerActor* owner = scene.SpawnActor<SensorsOwnerActor>("SensorsOwnerActor");
    ASSERT_NE(owner, nullptr);
    NS::HitSensorsObj sensors;

    NS::FollowHitSensor* attack =
        sensors.AddFollow(owner, "Attack", SensorKind::PlayerBody, [](const NS::Matrix& rootWorld) {
            return NS::SensorVolume::Sphere(rootWorld.Translation(), 0.25f);
        });
    ASSERT_NE(attack, nullptr);
    EXPECT_EQ(attack->Name(), "Attack");
    EXPECT_TRUE(GL::Level::IsSensorKind(attack, SensorKind::PlayerBody));
    EXPECT_EQ(attack->Host(), owner);
    EXPECT_EQ(ListedCount(scene, attack), 1u);
    owner->Root().SetPosition(Vector3{1.0f, 2.0f, 3.0f});
    const NS::SensorVolume volume = NS::HitSensorDirector::VolumeOf(attack);
    EXPECT_FLOAT_EQ(volume.radius, 0.25f);
    EXPECT_FLOAT_EQ(volume.Center().y, 2.0f);
    sensors.EndAll(owner);
}

// 同じ名前の 2 つ目は ERROR を出して作らない。1 つ目は残り、管理クラスにも 1 つだけ入っている
TEST(HitSensorsObj, SecondSensorOfTheSameNameIsRefused)
{
    NS::Scene scene;
    SensorsOwnerActor* owner = scene.SpawnActor<SensorsOwnerActor>("SensorsOwnerActor");
    ASSERT_NE(owner, nullptr);
    NS::HitSensorsObj sensors;
    NS::ShapeHitSensor* first = sensors.AddShape(owner, "Body", SensorKind::Area, NS::HitSensorShape::Box);
    const std::size_t listed = scene.HitSensors().Sensors().size();

    const TestLogCapture capture;
    NS::ShapeHitSensor* second = sensors.AddShape(owner, "Body", SensorKind::Area, NS::HitSensorShape::Sphere);

    EXPECT_EQ(second, nullptr);
    EXPECT_EQ(capture.ErrorCount(), 1);
    EXPECT_EQ(sensors.Find("Body"), first);
    EXPECT_EQ(sensors.Sensors().size(), 1u);
    EXPECT_EQ(scene.HitSensors().Sensors().size(), listed);
    sensors.EndAll(owner);
}

// 出入りの関数は並びの全部のセンサーに効き、EndAll は全部を管理クラスから外す
TEST(HitSensorsObj, EnableDisableAndEndReachEverySensor)
{
    NS::Scene scene;
    SensorsOwnerActor* owner = scene.SpawnActor<SensorsOwnerActor>("SensorsOwnerActor");
    ASSERT_NE(owner, nullptr);
    const std::size_t before = scene.HitSensors().Sensors().size();
    NS::HitSensorsObj sensors;
    NS::ShapeHitSensor* body = sensors.AddShape(owner, "Body", SensorKind::Area, NS::HitSensorShape::Box);
    NS::ShapeHitSensor* area = sensors.AddShape(owner, "Area", SensorKind::Area, NS::HitSensorShape::Sphere);
    ASSERT_NE(body, nullptr);
    ASSERT_NE(area, nullptr);

    sensors.EnableAll();
    EXPECT_TRUE(body->IsValidBySystem());
    EXPECT_TRUE(area->IsValidBySystem());
    sensors.DisableAll();
    EXPECT_FALSE(body->IsValidBySystem());
    EXPECT_FALSE(area->IsValidBySystem());
    EXPECT_EQ(scene.HitSensors().Sensors().size(), before + 2);
    sensors.EndAll(owner);
    EXPECT_EQ(scene.HitSensors().Sensors().size(), before);
}

// .actor の hitSensors の名前ごとの値を、同じ名前のセンサーへ書く。無い名前は WARN を出して読まない
TEST(HitSensorsObj, ReloadValuesWritesEachNamedSensor)
{
    const ScopedActorFileDirectory scope("HitSensorsReload");
    const nlohmann::json* goal = NS::ActorFileLibrary::Get().Find("GoalActor");
    ASSERT_NE(goal, nullptr);
    nlohmann::json actorFile = *goal;
    actorFile["hitSensors"] = nlohmann::json{
        {"Body", GoalBodyValues()}, {"Zone", DeathZoneBodyValues()}, {"Missing", GoalBodyValues()}};
    const ScopedActorFile file("HitSensorsProbe", actorFile);
    const std::unique_ptr<NS::SceneActor> owner = NS::CreateActorWithCodeValues("GoalActor", "HitSensorsProbe");
    ASSERT_NE(owner, nullptr);
    NS::HitSensorsObj sensors;
    NS::ShapeHitSensor* body = sensors.AddShape(owner.get(), "Body", SensorKind::Area, NS::HitSensorShape::Capsule);
    NS::ShapeHitSensor* zone = sensors.AddShape(owner.get(), "Zone", SensorKind::Area, NS::HitSensorShape::Box);
    ASSERT_NE(body, nullptr);
    ASSERT_NE(zone, nullptr);

    const TestLogCapture capture;
    sensors.ReloadValues(owner.get());

    EXPECT_FLOAT_EQ(body->Radius(), 0.85f);
    EXPECT_FLOAT_EQ(body->HalfHeight(), 0.5f);
    EXPECT_FLOAT_EQ(body->CenterOffset().y, 0.3f);
    EXPECT_FLOAT_EQ(body->BoxHalfExtents().x, 0.5f);
    EXPECT_FLOAT_EQ(zone->Radius(), 0.5f);
    EXPECT_FLOAT_EQ(zone->BoxHalfExtents().x, 1000.0f);
    EXPECT_FLOAT_EQ(zone->BoxHalfExtents().y, 5.0f);
    EXPECT_FLOAT_EQ(zone->CenterOffset().y, 0.0f);
    EXPECT_EQ(capture.WarningCount(), 1);
}

// hitSensors に無い値を持つセンサーは、宣言した時の値へ戻して WARN を出す。前に読んだ値を残さない
// 取り消しで hitSensors が消えた時に、ファイルに無い大きさで当たり続けないため
TEST(HitSensorsObj, ReloadValuesResetsASensorMissingFromTheFile)
{
    const ScopedActorFileDirectory scope("HitSensorsReset");
    const nlohmann::json* goal = NS::ActorFileLibrary::Get().Find("GoalActor");
    ASSERT_NE(goal, nullptr);
    nlohmann::json actorFile = *goal;
    actorFile["hitSensors"] = nlohmann::json{{"Body", GoalBodyValues()}};
    const ScopedActorFile file("HitSensorsResetProbe", actorFile);
    const std::unique_ptr<NS::SceneActor> owner = NS::CreateActorWithCodeValues("GoalActor", "HitSensorsResetProbe");
    ASSERT_NE(owner, nullptr);
    NS::HitSensorsObj sensors;
    NS::ShapeHitSensor* body = sensors.AddShape(owner.get(), "Body", SensorKind::Area, NS::HitSensorShape::Sphere);
    ASSERT_NE(body, nullptr);
    const float declared = body->Radius();
    sensors.ReloadValues(owner.get());
    ASSERT_FLOAT_EQ(body->Radius(), 0.85f);

    actorFile.erase("hitSensors");
    ASSERT_TRUE(NS::ActorFileLibrary::Get().Set("HitSensorsResetProbe", actorFile));
    const TestLogCapture capture;
    sensors.ReloadValues(owner.get());

    EXPECT_FLOAT_EQ(body->Radius(), declared);
    EXPECT_EQ(capture.WarningCount(), 1);
}

// 書き戻しは、部品が持たない名前の hitSensors の値を字面のまま残す。読まなかった本人の値を消さない
TEST(HitSensorsObj, WriteValuesKeepsEntriesItDidNotRead)
{
    NS::Scene scene;
    SensorsOwnerActor* owner = scene.SpawnActor<SensorsOwnerActor>("SensorsOwnerActor");
    ASSERT_NE(owner, nullptr);
    NS::HitSensorsObj sensors;
    ASSERT_NE(sensors.AddShape(owner, "Body", SensorKind::Area, NS::HitSensorShape::Sphere), nullptr);

    nlohmann::json actorFile = nlohmann::json{{"class", "GoalActor"}, {"hitSensors", {{"Old", GoalBodyValues()}}}};
    sensors.WriteValues(actorFile);

    const nlohmann::json& written = actorFile["hitSensors"];
    ASSERT_TRUE(written.contains("Old"));
    EXPECT_EQ(written["Old"], GoalBodyValues());
    EXPECT_TRUE(written.contains("Body"));
    sensors.EndAll(owner);
}

// 出荷の .actor から作ったゴールと落下の区域の体のセンサーは、hitSensors へ移す前の BodySensor の大きさのまま
TEST(HitSensorsObj, ShippedGoalAndDeathZoneKeepTheirSensorSizes)
{
    const std::unique_ptr<GL::Level::GoalActor> goal = NS::CreateActor<GL::Level::GoalActor>();
    const std::unique_ptr<GL::Level::DeathZoneActor> zone = NS::CreateActor<GL::Level::DeathZoneActor>();
    ASSERT_NE(goal, nullptr);
    ASSERT_NE(zone, nullptr);
    const NS::ShapeHitSensor* goalBody = NS::Cast<NS::ShapeHitSensor>(goal->HitSensorsObjClassObj()->Find("Body"));
    const NS::ShapeHitSensor* zoneBody = NS::Cast<NS::ShapeHitSensor>(zone->HitSensorsObjClassObj()->Find("Body"));
    ASSERT_NE(goalBody, nullptr);
    ASSERT_NE(zoneBody, nullptr);
    ExpectSizes(*goalBody, GoalBodyValues());
    ExpectSizes(*zoneBody, DeathZoneBodyValues());
}

// 書き戻しは読んだのと同じ形の hitSensors を書く。値を持たないセンサーは書かない
TEST(HitSensorsObj, WriteValuesWritesTheSameShapeItReads)
{
    NS::Scene scene;
    SensorsOwnerActor* owner = scene.SpawnActor<SensorsOwnerActor>("SensorsOwnerActor");
    ASSERT_NE(owner, nullptr);
    NS::HitSensorsObj sensors;
    NS::ShapeHitSensor* body = sensors.AddShape(owner, "Body", SensorKind::Area, NS::HitSensorShape::Capsule);
    ASSERT_NE(body, nullptr);
    body->SetCapsule(0.85f, 0.5f);
    body->SetCenterOffset(Vector3{0.0f, 0.3f, 0.0f});
    (void)sensors.AddFollow(owner, "Attack", SensorKind::PlayerBody, [](const NS::Matrix& rootWorld) {
        return NS::SensorVolume::Sphere(rootWorld.Translation(), 0.25f);
    });

    nlohmann::json actorFile = nlohmann::json{{"class", "GoalActor"}};
    sensors.WriteValues(actorFile);

    ASSERT_TRUE(actorFile.contains("hitSensors"));
    const nlohmann::json& written = actorFile["hitSensors"];
    EXPECT_EQ(written.size(), 1u);
    ASSERT_TRUE(written.contains("Body"));
    EXPECT_FLOAT_EQ(written["Body"]["半径"].get<float>(), 0.85f);
    EXPECT_FLOAT_EQ(written["Body"]["半分の高さ"].get<float>(), 0.5f);
    EXPECT_FLOAT_EQ(written["Body"]["中心オフセット"][1].get<float>(), 0.3f);
    EXPECT_TRUE(written["Body"].contains("箱の半径"));
    sensors.EndAll(owner);
}
