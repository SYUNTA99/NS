#include "Game/Level/DeathZoneActor.h"
#include "Game/Level/GoalActor.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IGet/IGetHitSensor.h"
#include "NSlib/CoreObject/Message/Message.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"
#include "NSlib/Phys/Capsule.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

// ヒットセンサーの形の重なり・調べ役の呼び出し・範囲の受け手の照合と、知らせの送り方を縛る

namespace
{
    using NS::Vector3;
    using GL::Level::SensorKind;

    // 重なりの始まり・続き・終わりの相手を控える試しの SceneActor
    class SensorProbeActor final : public NS::SceneActor
    {
    public:
        SensorProbeActor(SensorKind kind, float radius) : m_kind(kind), m_radius(radius) {}

        void OnSensorEnter(NS::HitSensor* self, NS::HitSensor* other) override
        {
            NS_ASSERT(Game, self != nullptr && other != nullptr, "重なったセンサーが渡されていない");
            selves.push_back(self);
            entered.push_back(other);
        }
        void OnSensorStay(NS::HitSensor* self, NS::HitSensor* other) override
        {
            NS_ASSERT(Game, self != nullptr && other != nullptr, "重なったセンサーが渡されていない");
            stayed.push_back(other);
        }
        // 相手が一覧から外れた時の other は nullptr
        void OnSensorExit(NS::HitSensor* self, NS::HitSensor* other) override
        {
            NS_ASSERT(Game, self != nullptr, "終わった組の自分のセンサーが渡されていない");
            exited.push_back(other);
        }

        bool ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver) override
        {
            (void)sender;
            (void)receiver;
            received.push_back(msg.Kind());
            return acceptMessages;
        }

        NS::ShapeHitSensor* m_sensor = nullptr;
        std::vector<NS::HitSensor*> selves; // 始まりの自分のセンサー
        std::vector<NS::HitSensor*> entered;
        std::vector<NS::HitSensor*> stayed;
        std::vector<NS::HitSensor*> exited;
        std::vector<const void*> received;
        bool acceptMessages = true;

    protected:
        void OnInit() override
        {
            m_sensor = NS::Cast<NS::ShapeHitSensor>(m_bodySensor);
            m_sensor->SetSphere(m_radius);
        }

    private:
        // 種類はコンストラクタで受けた物。宣言はコンストラクタの後に読まれる
        NS_SENSORS_BEGIN()
        NS_SENSOR(m_bodySensor, "Body", m_kind, NS::HitSensorShape::Sphere)
        NS_SENSORS_END()
        NS::HitSensor* m_bodySensor = nullptr;

        SensorKind m_kind;
        float m_radius;
    };

    class MsgPing final : public NS::Message
    {
        NS_MESSAGE(MsgPing)
    };

    class MsgPong final : public NS::Message
    {
        NS_MESSAGE(MsgPong)
    };
} // namespace

TEST(SensorVolume, SpheresTouchAtSumOfRadii)
{
    const NS::SensorVolume a = NS::SensorVolume::Sphere(Vector3{0.0f, 0.0f, 0.0f}, 1.0f);
    EXPECT_TRUE(NS::VolumesOverlap(a, NS::SensorVolume::Sphere(Vector3{1.5f, 0.0f, 0.0f}, 0.5f)));
    EXPECT_FALSE(NS::VolumesOverlap(a, NS::SensorVolume::Sphere(Vector3{1.6f, 0.0f, 0.0f}, 0.5f)));
}

TEST(SensorVolume, CapsuleUsesItsSegment)
{
    // 縦に伸びたカプセルは、中心から離れた高さの球にも触れる
    const NS::SensorVolume capsule = NS::SensorVolume::Capsule(NS::Phys::Capsule{
        .center = Vector3{0.0f, 0.0f, 0.0f}, .axis = Vector3::UnitY, .halfHeight = 2.0f, .radius = 0.5f});
    EXPECT_TRUE(NS::VolumesOverlap(capsule, NS::SensorVolume::Sphere(Vector3{0.8f, 1.9f, 0.0f}, 0.4f)));
    EXPECT_FALSE(NS::VolumesOverlap(capsule, NS::SensorVolume::Sphere(Vector3{0.8f, 3.0f, 0.0f}, 0.2f)));
}

TEST(SensorVolume, BoxAgainstSphereAndCapsule)
{
    const NS::SensorVolume box = NS::SensorVolume::Box(
        NS::MakeOBB(Vector3{0.0f, 0.0f, 0.0f}, NS::Quaternion::Identity, Vector3{10.0f, 1.0f, 10.0f}));
    EXPECT_TRUE(NS::VolumesOverlap(box, NS::SensorVolume::Sphere(Vector3{3.0f, 1.4f, 3.0f}, 0.5f)));
    EXPECT_FALSE(NS::VolumesOverlap(box, NS::SensorVolume::Sphere(Vector3{3.0f, 1.6f, 3.0f}, 0.5f)));
    const NS::SensorVolume capsule = NS::SensorVolume::Capsule(NS::Phys::Capsule{
        .center = Vector3{0.0f, 2.2f, 0.0f}, .axis = Vector3::UnitY, .halfHeight = 0.5f, .radius = 0.8f});
    EXPECT_TRUE(NS::VolumesOverlap(capsule, box));
}

TEST(SensorVolume, BoxesUseSeparatingAxes)
{
    const NS::SensorVolume a = NS::SensorVolume::Box(
        NS::MakeOBB(Vector3{0.0f, 0.0f, 0.0f}, NS::Quaternion::Identity, Vector3{1.0f, 1.0f, 1.0f}));
    // 45 度回した箱は、角が軸並行の外接箱より内側にある
    const NS::Quaternion turned = NS::Quaternion::CreateFromYawPitchRoll(NS::k_Pi * 0.25f, 0.0f, 0.0f);
    EXPECT_TRUE(NS::VolumesOverlap(
        a, NS::SensorVolume::Box(NS::MakeOBB(Vector3{2.3f, 0.0f, 0.0f}, turned, Vector3{1.0f, 1.0f, 1.0f}))));
    EXPECT_FALSE(NS::VolumesOverlap(
        a, NS::SensorVolume::Box(NS::MakeOBB(Vector3{2.5f, 0.0f, 0.0f}, turned, Vector3{1.0f, 1.0f, 1.0f}))));
}

TEST(MakeScaledCapsule, RadiusTakesTheLargerSideScale)
{
    const NS::Quaternion turned = NS::Quaternion::CreateFromYawPitchRoll(0.0f, 0.0f, NS::k_Pi * 0.5f);
    const NS::Phys::Capsule capsule =
        NS::Phys::MakeScaledCapsule(Vector3{1.0f, 2.0f, 3.0f}, turned, Vector3{-2.0f, 3.0f, 0.5f}, 0.5f, 1.0f);
    EXPECT_FLOAT_EQ(capsule.center.y, 2.0f);
    EXPECT_FLOAT_EQ(capsule.halfHeight, 3.0f);
    EXPECT_FLOAT_EQ(capsule.radius, 1.0f);
    EXPECT_NEAR(capsule.axis.x, -1.0f, 1e-5f);

    const NS::Phys::Capsule deeper = NS::Phys::MakeScaledCapsule(
        Vector3{0.0f, 0.0f, 0.0f}, NS::Quaternion::Identity, Vector3{0.5f, 3.0f, -2.0f}, 0.5f, 1.0f);
    EXPECT_FLOAT_EQ(deeper.radius, 1.0f);
}

TEST(HitSensor, WorldVolumeFollowsRootScale)
{
    NS::Scene scene;
    SensorProbeActor* spawned = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 0.5f);
    ASSERT_NE(spawned, nullptr);
    SensorProbeActor& actor = *spawned;
    NS::ShapeHitSensor* sensor = actor.m_sensor;
    ASSERT_NE(sensor, nullptr);
    sensor->SetSphere(0.5f);
    actor.Root().SetPosition(Vector3{1.0f, 2.0f, 3.0f});
    actor.Root().SetScale(Vector3{2.0f, 2.0f, 2.0f});
    const NS::SensorVolume volume = sensor->WorldVolume(actor.Root().WorldMatrix());
    EXPECT_FLOAT_EQ(volume.radius, 1.0f);
    EXPECT_FLOAT_EQ(volume.Center().y, 2.0f);
}

TEST(HitSensor, ShapeSensorKeepsItsFourSavedFieldLabels)
{
    // 欄の表示名は保存の鍵。ゴールと落下死の範囲の保存済みの値がこの 4 つで読まれる
    const std::unique_ptr<SensorProbeActor> owned = NS::CreateActor<SensorProbeActor>(SensorKind::Area, 0.5f);
    SensorProbeActor& actor = *owned;
    NS::Object* sensor = actor.m_sensor;
    ASSERT_NE(sensor, nullptr);
    const NS::ReflectionInfo* info = sensor->GetReflection();
    std::vector<std::string> labels;
    for (std::size_t i = 0; i < info->fieldCount; ++i)
    {
        labels.emplace_back(info->fields[i].name);
    }
    EXPECT_EQ(labels, (std::vector<std::string>{"半径", "半分の高さ", "箱の半径", "中心オフセット"}));
}

TEST(HitSensor, BaseSensorHoldsNoFields)
{
    EXPECT_EQ(NS::HitSensor::StaticReflection()->fieldCount, 0u);
}

TEST(HitSensorDirector, TickTellsEveryOverlappingPairWhateverTheKinds)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);
    SensorProbeActor* rock = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::MapObjBody, 0.5f);
    body->Root().SetPosition(Vector3{1.0f, 0.0f, 0.0f});
    rock->Root().SetPosition(Vector3{0.5f, 0.0f, 0.0f});

    scene.HitSensors().OnTick();

    // 3 つとも重なっている。種類は見ないので、どれも残りの 2 つの始まりを登録順に聞く
    EXPECT_EQ(area->entered, (std::vector<NS::HitSensor*>{body->m_sensor, rock->m_sensor}));
    EXPECT_EQ(body->entered, (std::vector<NS::HitSensor*>{area->m_sensor, rock->m_sensor}));
    EXPECT_EQ(rock->entered, (std::vector<NS::HitSensor*>{area->m_sensor, body->m_sensor}));
}

// 重なる → 2 固定フレーム続く → 離れる で、始まり 1・続き 2・終わり 1。離れた後は何も届かない
TEST(HitSensorDirector, OverlapEntersOnceStaysThenExitsOnce)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);
    body->Root().SetPosition(Vector3{1.0f, 0.0f, 0.0f});

    scene.HitSensors().OnTick();
    EXPECT_EQ(area->entered, (std::vector<NS::HitSensor*>{body->m_sensor}));
    EXPECT_TRUE(area->stayed.empty());
    EXPECT_TRUE(area->exited.empty());

    scene.HitSensors().OnTick();
    scene.HitSensors().OnTick();
    EXPECT_EQ(area->entered.size(), 1u);
    EXPECT_EQ(area->stayed, (std::vector<NS::HitSensor*>{body->m_sensor, body->m_sensor}));
    EXPECT_EQ(body->stayed, (std::vector<NS::HitSensor*>{area->m_sensor, area->m_sensor}));
    EXPECT_TRUE(area->exited.empty());

    body->Root().SetPosition(Vector3{5.0f, 0.0f, 0.0f});
    scene.HitSensors().OnTick();
    scene.HitSensors().OnTick();
    EXPECT_EQ(area->entered.size(), 1u);
    EXPECT_EQ(area->stayed.size(), 2u);
    EXPECT_EQ(area->exited, (std::vector<NS::HitSensor*>{body->m_sensor}));
    EXPECT_EQ(body->exited, (std::vector<NS::HitSensor*>{area->m_sensor}));
}

TEST(HitSensorDirector, InvalidSensorIsNotChecked)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);
    body->m_sensor->Invalidate();
    scene.HitSensors().OnTick();
    EXPECT_TRUE(area->entered.empty());
    EXPECT_TRUE(body->entered.empty());
}

// 重なっている間に Invalidate すると、次の固定フレームに両方へ終わりが 1 回ずつ届く。Validate で戻せば始まりからやり直す
TEST(HitSensorDirector, InvalidatedSensorEndsTheOverlapOnce)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);
    scene.HitSensors().OnTick();
    ASSERT_EQ(area->entered.size(), 1u);

    body->m_sensor->Invalidate();
    scene.HitSensors().OnTick();
    scene.HitSensors().OnTick();
    EXPECT_EQ(area->exited, (std::vector<NS::HitSensor*>{body->m_sensor}));
    EXPECT_EQ(body->exited, (std::vector<NS::HitSensor*>{area->m_sensor}));
    EXPECT_TRUE(area->stayed.empty());

    body->m_sensor->Validate();
    scene.HitSensors().OnTick();
    EXPECT_EQ(area->entered.size(), 2u);
}

// 持ち主が消えた時も、両方へ終わりが 1 回ずつ届く。消えている間は何も届かない
TEST(HitSensorDirector, DisappearedOwnerEndsTheOverlapOnce)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);
    scene.HitSensors().OnTick();
    ASSERT_EQ(area->entered.size(), 1u);

    body->Disappear();
    scene.HitSensors().OnTick();
    scene.HitSensors().OnTick();
    EXPECT_EQ(area->exited, (std::vector<NS::HitSensor*>{body->m_sensor}));
    EXPECT_EQ(body->exited, (std::vector<NS::HitSensor*>{area->m_sensor}));
    EXPECT_TRUE(area->stayed.empty());
    EXPECT_TRUE(body->stayed.empty());
}

// 重なっている相手のアクターが終わって解放されると、残った側へ終わりが 1 回だけ、相手を nullptr で届く
TEST(HitSensorDirector, RemovedActorEndsTheOverlapForThePartnerOnce)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);
    scene.HitSensors().OnTick();
    ASSERT_EQ(area->entered.size(), 1u);

    scene.RemoveActor(body->Id());
    scene.HitSensors().OnTick();
    scene.HitSensors().OnTick();
    EXPECT_EQ(area->exited, (std::vector<NS::HitSensor*>{nullptr}));
    EXPECT_TRUE(area->stayed.empty());
}

// 一覧から外したセンサーの持ち主には外したその場で終わりが届き、相手には次の固定フレームに相手を nullptr で届く。
// どちらも 1 回だけ
TEST(HitSensorDirector, UnregisteredSensorEndsTheOverlapForBothOwners)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);
    scene.HitSensors().OnTick();
    ASSERT_EQ(area->entered.size(), 1u);

    scene.HitSensors().Unregister(body->m_sensor);
    EXPECT_EQ(body->exited, (std::vector<NS::HitSensor*>{area->m_sensor}));
    scene.HitSensors().OnTick();
    scene.HitSensors().OnTick();
    EXPECT_EQ(area->exited, (std::vector<NS::HitSensor*>{nullptr}));
    EXPECT_TRUE(area->stayed.empty());
    EXPECT_EQ(body->exited.size(), 1u);
    EXPECT_TRUE(body->stayed.empty());

    // 戻すと始まりからやり直す
    scene.HitSensors().Register(body->m_sensor);
    scene.HitSensors().OnTick();
    EXPECT_EQ(area->entered.size(), 2u);
}

// 相手が先に外れて終わりが予約された組も、次の固定フレームの前に自分が外れれば、その場で 1 回届く
TEST(HitSensorDirector, BothSensorsUnregisteredInOneFrameEachGetOneExit)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);
    scene.HitSensors().OnTick();
    ASSERT_EQ(area->entered.size(), 1u);

    scene.HitSensors().Unregister(body->m_sensor);
    scene.HitSensors().Unregister(area->m_sensor);
    scene.HitSensors().OnTick();
    EXPECT_EQ(body->exited, (std::vector<NS::HitSensor*>{area->m_sensor}));
    EXPECT_EQ(area->exited, (std::vector<NS::HitSensor*>{nullptr}));
}

// 終わるアクターも、始まりを受けた組の終わりを OnEndPlay の中で 1 回受ける
TEST(HitSensorDirector, EndingActorGetsItsOwnExitOnce)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);
    scene.HitSensors().OnTick();
    ASSERT_EQ(body->entered.size(), 1u);

    body->OnEndPlay();
    EXPECT_EQ(body->exited, (std::vector<NS::HitSensor*>{area->m_sensor}));
    scene.HitSensors().OnTick();
    EXPECT_EQ(body->exited.size(), 1u);
    EXPECT_EQ(area->exited, (std::vector<NS::HitSensor*>{nullptr}));
}

// 始まりを受けた組には、外れ方が重なっても終わりは 1 回だけ届く
TEST(HitSensorDirector, EveryEnteredPairGetsExactlyOneExit)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);
    scene.HitSensors().OnTick();
    ASSERT_EQ(area->entered.size(), 1u);

    // 同じ固定フレームの間に、離れて・外されて・消えて・解放される
    body->Root().SetPosition(Vector3{5.0f, 0.0f, 0.0f});
    body->m_sensor->Invalidate();
    body->Disappear();
    scene.RemoveActor(body->Id());
    scene.HitSensors().OnTick();
    scene.HitSensors().OnTick();
    EXPECT_EQ(area->exited.size(), 1u);
}

// 重なった組は両方の持ち主へ、自分と相手を入れ替えて 1 回ずつ知らせる。応じるかは受け手が相手の種類で決める
TEST(HitSensorDirector, BothOwnersHearAnOverlap)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);
    body->Root().SetPosition(Vector3{1.0f, 0.0f, 0.0f});

    scene.HitSensors().OnTick();

    ASSERT_EQ(area->entered.size(), 1u);
    EXPECT_EQ(area->selves[0], area->m_sensor);
    EXPECT_EQ(area->entered[0], body->m_sensor);
    ASSERT_EQ(body->entered.size(), 1u);
    EXPECT_EQ(body->selves[0], body->m_sensor);
    EXPECT_EQ(body->entered[0], area->m_sensor);
}

// ゴールは自機の体にだけ、触れ始めた時に 1 回知らせる。岩の体が範囲に重なっても知らせは出ない
TEST(GoalActor, IgnoresARockBody)
{
    NS::Scene scene;
    (void)scene.SpawnActor<GL::Level::GoalActor>("GoalActor");
    SensorProbeActor* rock = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::MapObjBody, 0.5f);
    SensorProbeActor* body = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);

    scene.HitSensors().OnTick();
    scene.HitSensors().OnTick();

    EXPECT_TRUE(rock->received.empty());
    // 同じ所の自機の体には届く。照合が相手の種類を見ている証し
    ASSERT_EQ(body->received.size(), 1u);
    EXPECT_EQ(body->received[0], GL::Level::MsgGoal::StaticKind());
}

// 落下死の範囲も自機の体にだけ、入った時に 1 回知らせる
TEST(DeathZoneActor, IgnoresARockBody)
{
    NS::Scene scene;
    (void)scene.SpawnActor<GL::Level::DeathZoneActor>("DeathZoneActor");
    SensorProbeActor* rock = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::MapObjBody, 0.5f);
    SensorProbeActor* body = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);

    scene.HitSensors().OnTick();
    scene.HitSensors().OnTick();

    EXPECT_TRUE(rock->received.empty());
    ASSERT_EQ(body->received.size(), 1u);
    EXPECT_EQ(body->received[0], GL::Level::MsgInstantDeath::StaticKind());
}

// 先読みの問いは仕組みの条件だけで絞る。種類は問う側が見る
TEST(HitSensorDirector, FindOverlapsReturnsValidSensorsAndSkipsTheIgnoredOwner)
{
    NS::Scene scene;
    SensorProbeActor* rock = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::MapObjBody, 0.5f);
    SensorProbeActor* area = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 0.5f);
    const NS::SensorVolume probe = NS::SensorVolume::Sphere(Vector3{0.0f, 0.0f, 0.0f}, 0.5f);

    EXPECT_EQ(scene.HitSensors().FindOverlaps(probe, nullptr),
              (std::vector<NS::HitSensor*>{rock->m_sensor, area->m_sensor}));
    EXPECT_EQ(scene.HitSensors().FindOverlaps(probe, rock), (std::vector<NS::HitSensor*>{area->m_sensor}));
    area->m_sensor->Invalidate();
    EXPECT_TRUE(scene.HitSensors().FindOverlaps(probe, rock).empty());
}

TEST(HitSensorDirector, SensorLeavesDirectorWhenDestroyed)
{
    NS::Scene scene;
    const std::size_t before = scene.HitSensors().Sensors().size();
    SensorProbeActor* rock = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::MapObjBody, 0.5f);
    EXPECT_EQ(scene.HitSensors().Sensors().size(), before + 1);
    rock->OnEndPlay();
    EXPECT_EQ(scene.HitSensors().Sensors().size(), before);
}

// 登録されているかは調べ役の並びだけが持つ。外したセンサーは次の Register で戻り、二重には入らない
TEST(HitSensorDirector, SensorRejoinsAfterTheDirectorDroppedIt)
{
    NS::Scene scene;
    SensorProbeActor* rock = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::MapObjBody, 0.5f);
    const std::size_t joined = scene.HitSensors().Sensors().size();
    scene.HitSensors().Unregister(rock->m_sensor);
    ASSERT_EQ(scene.HitSensors().Sensors().size(), joined - 1);
    scene.HitSensors().Register(rock->m_sensor);
    scene.HitSensors().Register(rock->m_sensor);
    EXPECT_EQ(scene.HitSensors().Sensors().size(), joined);
}

// 調べる対象かは、出入りで切り替える値とゲームの Invalidate の両方で決まり、持ち主が出ているかを読まない
TEST(HitSensor, ValidOnlyWhileEnabledAndNotInvalidated)
{
    NS::ShapeHitSensor loose{nullptr, "Body", 0, NS::HitSensorShape::Sphere};
    EXPECT_FALSE(loose.IsValid());
    NS::EnableHitSensor(&loose);
    EXPECT_TRUE(loose.IsValid());
    loose.Invalidate();
    EXPECT_FALSE(loose.IsValid());
    loose.Validate();
    NS::DisableHitSensor(&loose);
    EXPECT_FALSE(loose.IsValid());

    NS::Scene scene;
    SensorProbeActor* rock = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::MapObjBody, 0.5f);
    ASSERT_TRUE(rock->IsAppeared());
    ASSERT_TRUE(rock->m_sensor->IsValid());
    NS::DisableHitSensor(rock->m_sensor);
    EXPECT_FALSE(rock->m_sensor->IsValid());
}

TEST(Message, KindsAreDistinctPerType)
{
    const MsgPing ping;
    const MsgPong pong;
    EXPECT_TRUE(NS::IsMsg<MsgPing>(ping));
    EXPECT_FALSE(NS::IsMsg<MsgPing>(pong));
    EXPECT_NE(NS::MsgCast<MsgPong>(pong), nullptr);
    EXPECT_EQ(NS::MsgCast<MsgPong>(ping), nullptr);
}

TEST(Message, SendReachesReceiverOwnerAndReturnsItsAnswer)
{
    NS::Scene scene;
    SensorProbeActor* sender = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::Area, 1.0f);
    SensorProbeActor* receiver = scene.SpawnActor<SensorProbeActor>("SensorProbeActor", SensorKind::PlayerBody, 0.5f);

    EXPECT_TRUE(NS::SendMsg(MsgPing{}, receiver->m_sensor, sender->m_sensor));
    ASSERT_EQ(receiver->received.size(), 1u);
    EXPECT_EQ(receiver->received[0], MsgPing::StaticKind());

    // 受け手が応じなければ false
    receiver->acceptMessages = false;
    EXPECT_FALSE(NS::SendMsg(MsgPong{}, receiver->m_sensor, sender->m_sensor));

    // 自分宛ては送らない
    EXPECT_FALSE(NS::SendMsg(MsgPing{}, sender->m_sensor, sender->m_sensor));
    EXPECT_TRUE(sender->received.empty());
}

namespace
{
    // 形の決まったセンサーと、形を関数から読むセンサーを宣言し、OnInit で見えた物を控える
    class DeclaredSensorsActor final : public NS::SceneActor
    {
    public:
        NS::HitSensor* bodyAtInit = nullptr;
        NS::HitSensor* attackAtInit = nullptr;
        bool bodyListedAtInit = false;

    protected:
        void OnInit() override
        {
            bodyAtInit = m_bodySensor;
            attackAtInit = m_attackSensor;
            if (NS::HitSensorDirector* director = GetHitSensorDirector())
            {
                const std::vector<NS::HitSensor*>& listed = director->Sensors();
                bodyListedAtInit = std::find(listed.begin(), listed.end(), bodyAtInit) != listed.end();
            }
        }

    private:
        [[nodiscard]] NS::SensorVolume AttackVolume(const NS::Matrix& rootWorld) const noexcept
        {
            return NS::SensorVolume::Sphere(rootWorld.Translation(), 0.25f);
        }

        NS_SENSORS_BEGIN()
        NS_SENSOR(m_bodySensor, "Body", SensorKind::Area, NS::HitSensorShape::Box)
        NS_FOLLOW_SENSOR(m_attackSensor, "Attack", SensorKind::PlayerBody, AttackVolume)
        NS_SENSORS_END()
        NS::HitSensor* m_bodySensor = nullptr;
        NS::HitSensor* m_attackSensor = nullptr;
    };
} // namespace

// 名前の違う 2 つの宣言は、OnInit の前にセンサーの部品に 2 つあり、宣言した名前・種類・形と持ち主を持ち、
// シーンの一覧に 1 回ずつ入る。マクロが指したメンバー変数は作ったセンサーを指す
TEST(HitSensorsObj, TwoDeclaredSensorsExistBeforeOnInitWithTheirNames)
{
    NS::Scene scene;
    DeclaredSensorsActor* actor = scene.SpawnActor<DeclaredSensorsActor>("DeclaredSensorsActor");
    ASSERT_NE(actor, nullptr);
    ASSERT_NE(actor->bodyAtInit, nullptr);
    ASSERT_NE(actor->attackAtInit, nullptr);
    EXPECT_TRUE(actor->bodyListedAtInit);
    const NS::HitSensorsObj* sensors = actor->HitSensorsObjClassObj();
    ASSERT_NE(sensors, nullptr);
    EXPECT_EQ(sensors->Sensors(), (std::vector<NS::HitSensor*>{actor->bodyAtInit, actor->attackAtInit}));
    EXPECT_EQ(sensors->Find("Body"), actor->bodyAtInit);
    EXPECT_EQ(sensors->Find("Attack"), actor->attackAtInit);

    const NS::ShapeHitSensor* body = NS::Cast<NS::ShapeHitSensor>(actor->bodyAtInit);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->Name(), "Body");
    EXPECT_EQ(body->Shape(), NS::HitSensorShape::Box);
    EXPECT_TRUE(GL::Level::IsSensorKind(body, SensorKind::Area));
    EXPECT_EQ(body->Host(), actor);

    const NS::FollowHitSensor* attack = NS::Cast<NS::FollowHitSensor>(actor->attackAtInit);
    ASSERT_NE(attack, nullptr);
    EXPECT_EQ(attack->Name(), "Attack");
    EXPECT_TRUE(GL::Level::IsSensorKind(attack, SensorKind::PlayerBody));
    EXPECT_EQ(attack->Host(), actor);
    actor->Root().SetPosition(Vector3{1.0f, 2.0f, 3.0f});
    const NS::SensorVolume volume = NS::HitSensorDirector::VolumeOf(attack);
    EXPECT_FLOAT_EQ(volume.radius, 0.25f);
    EXPECT_FLOAT_EQ(volume.Center().y, 2.0f);

    const std::vector<NS::HitSensor*>& listed = scene.HitSensors().Sensors();
    EXPECT_EQ(std::count(listed.begin(), listed.end(), actor->bodyAtInit), 1);
    EXPECT_EQ(std::count(listed.begin(), listed.end(), actor->attackAtInit), 1);
}
