#include "Game/Level/DeathZoneActor.h"
#include "Game/Level/GoalActor.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/Message/Message.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/HitSensor.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"
#include "NSlib/Physics/Capsule.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// ヒットセンサーの形の重なり・調べ役の呼び出し・範囲の受け手の照合と、知らせの送り方を縛る

namespace
{
    using NS::Vector3;
    using GL::Level::SensorKind;

    // 重なった相手を控える試しの SceneActor
    class SensorProbeActor final : public NS::SceneActor
    {
    public:
        SensorProbeActor(SensorKind kind, float radius) : m_kind(kind), m_radius(radius) {}

        void AttackSensor(NS::HitSensor& self, NS::HitSensor& other) override
        {
            selves.push_back(&self);
            touched.push_back(&other);
        }

        bool ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver) override
        {
            (void)sender;
            (void)receiver;
            received.push_back(msg.Kind());
            return acceptMessages;
        }

        NS::ShapeHitSensor* m_sensor = nullptr;
        std::vector<NS::HitSensor*> selves;
        std::vector<NS::HitSensor*> touched;
        std::vector<const void*> received;
        bool acceptMessages = true;

    protected:
        void OnInit() override
        {
            m_sensor = CreateSubObj<NS::HitSensor, NS::ShapeHitSensor>(m_bodySensor, this);
            GL::Level::SetSensorKind(*m_sensor, m_kind);
            m_sensor->SetSphere(m_radius);
        }

    private:
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
    const std::unique_ptr<SensorProbeActor> owned = NS::CreateActor<SensorProbeActor>(SensorKind::Area, 0.5f);
    SensorProbeActor& actor = *owned;
    NS::ShapeHitSensor* sensor = actor.m_sensor;
    ASSERT_NE(sensor, nullptr);
    sensor->SetSphere(0.5f);
    actor.Root().SetPosition(Vector3{1.0f, 2.0f, 3.0f});
    actor.Root().SetScale(Vector3{2.0f, 2.0f, 2.0f});
    const NS::SensorVolume volume = sensor->WorldVolume();
    EXPECT_FLOAT_EQ(volume.radius, 1.0f);
    EXPECT_FLOAT_EQ(volume.Center().y, 2.0f);
}

TEST(HitSensor, ShapeSensorKeepsItsFourSavedFieldLabels)
{
    // 欄の表示名は保存の鍵。ゴールと落下死の範囲の保存済みの値がこの 4 つで読まれる
    const std::unique_ptr<SensorProbeActor> owned = NS::CreateActor<SensorProbeActor>(SensorKind::Area, 0.5f);
    SensorProbeActor& actor = *owned;
    NS::SubObject* sensor = actor.m_sensor;
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

TEST(HitSensorDirector, TickCallsAttackSensorForEveryOverlappingPairWhateverTheKinds)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnTransient<SensorProbeActor>(SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnTransient<SensorProbeActor>(SensorKind::PlayerBody, 0.5f);
    SensorProbeActor* rock = scene.SpawnTransient<SensorProbeActor>(SensorKind::MapObjBody, 0.5f);
    body->Root().SetPosition(Vector3{1.0f, 0.0f, 0.0f});
    rock->Root().SetPosition(Vector3{0.5f, 0.0f, 0.0f});

    scene.HitSensors().OnTick();

    // 3 つとも重なっている。種類は見ないので、どれも残りの 2 つを登録順に聞く
    EXPECT_EQ(area->touched, (std::vector<NS::HitSensor*>{body->m_sensor, rock->m_sensor}));
    EXPECT_EQ(body->touched, (std::vector<NS::HitSensor*>{area->m_sensor, rock->m_sensor}));
    EXPECT_EQ(rock->touched, (std::vector<NS::HitSensor*>{area->m_sensor, body->m_sensor}));

    // 離れた組は呼ばれない
    area->touched.clear();
    body->touched.clear();
    rock->touched.clear();
    body->Root().SetPosition(Vector3{5.0f, 0.0f, 0.0f});
    scene.HitSensors().OnTick();
    EXPECT_EQ(area->touched, (std::vector<NS::HitSensor*>{rock->m_sensor}));
    EXPECT_TRUE(body->touched.empty());
    EXPECT_EQ(rock->touched, (std::vector<NS::HitSensor*>{area->m_sensor}));
}

TEST(HitSensorDirector, InvalidSensorIsNotChecked)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnTransient<SensorProbeActor>(SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnTransient<SensorProbeActor>(SensorKind::PlayerBody, 0.5f);
    body->m_sensor->Invalidate();
    scene.HitSensors().OnTick();
    EXPECT_TRUE(area->touched.empty());
    EXPECT_TRUE(body->touched.empty());
}

// 重なった組は両方の持ち主へ、自分と相手を入れ替えて 1 回ずつ知らせる。応じるかは受け手が相手の種類で決める
TEST(HitSensorDirector, BothOwnersHearAnOverlap)
{
    NS::Scene scene;
    SensorProbeActor* area = scene.SpawnTransient<SensorProbeActor>(SensorKind::Area, 1.0f);
    SensorProbeActor* body = scene.SpawnTransient<SensorProbeActor>(SensorKind::PlayerBody, 0.5f);
    body->Root().SetPosition(Vector3{1.0f, 0.0f, 0.0f});

    scene.HitSensors().OnTick();

    ASSERT_EQ(area->touched.size(), 1u);
    EXPECT_EQ(area->selves[0], area->m_sensor);
    EXPECT_EQ(area->touched[0], body->m_sensor);
    ASSERT_EQ(body->touched.size(), 1u);
    EXPECT_EQ(body->selves[0], body->m_sensor);
    EXPECT_EQ(body->touched[0], area->m_sensor);
}

// ゴールは自機の体にだけ知らせる。岩の体が範囲に重なっても知らせは出ない
TEST(GoalActor, IgnoresARockBody)
{
    NS::Scene scene;
    (void)scene.SpawnTransient<GL::Level::GoalActor>();
    SensorProbeActor* rock = scene.SpawnTransient<SensorProbeActor>(SensorKind::MapObjBody, 0.5f);
    SensorProbeActor* body = scene.SpawnTransient<SensorProbeActor>(SensorKind::PlayerBody, 0.5f);

    scene.HitSensors().OnTick();

    EXPECT_TRUE(rock->received.empty());
    // 同じ所の自機の体には届く。照合が相手の種類を見ている証し
    ASSERT_EQ(body->received.size(), 1u);
    EXPECT_EQ(body->received[0], GL::Level::MsgGoal::StaticKind());
}

// 落下死の範囲も自機の体にだけ知らせる
TEST(DeathZoneActor, IgnoresARockBody)
{
    NS::Scene scene;
    (void)scene.SpawnTransient<GL::Level::DeathZoneActor>();
    SensorProbeActor* rock = scene.SpawnTransient<SensorProbeActor>(SensorKind::MapObjBody, 0.5f);
    SensorProbeActor* body = scene.SpawnTransient<SensorProbeActor>(SensorKind::PlayerBody, 0.5f);

    scene.HitSensors().OnTick();

    EXPECT_TRUE(rock->received.empty());
    ASSERT_EQ(body->received.size(), 1u);
    EXPECT_EQ(body->received[0], GL::Level::MsgInstantDeath::StaticKind());
}

// 先読みの問いは仕組みの条件だけで絞る。種類は問う側が見る
TEST(HitSensorDirector, FindOverlapsReturnsValidSensorsAndSkipsTheIgnoredOwner)
{
    NS::Scene scene;
    SensorProbeActor* rock = scene.SpawnTransient<SensorProbeActor>(SensorKind::MapObjBody, 0.5f);
    SensorProbeActor* area = scene.SpawnTransient<SensorProbeActor>(SensorKind::Area, 0.5f);
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
    SensorProbeActor* rock = scene.SpawnTransient<SensorProbeActor>(SensorKind::MapObjBody, 0.5f);
    EXPECT_EQ(scene.HitSensors().Sensors().size(), before + 1);
    rock->OnEndPlay();
    EXPECT_EQ(scene.HitSensors().Sensors().size(), before);
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
    SensorProbeActor* sender = scene.SpawnTransient<SensorProbeActor>(SensorKind::Area, 1.0f);
    SensorProbeActor* receiver = scene.SpawnTransient<SensorProbeActor>(SensorKind::PlayerBody, 0.5f);

    EXPECT_TRUE(NS::SendMsg(MsgPing{}, *receiver->m_sensor, sender->m_sensor));
    ASSERT_EQ(receiver->received.size(), 1u);
    EXPECT_EQ(receiver->received[0], MsgPing::StaticKind());

    // 受け手が応じなければ false
    receiver->acceptMessages = false;
    EXPECT_FALSE(NS::SendMsg(MsgPong{}, *receiver->m_sensor, sender->m_sensor));

    // 自分宛ては送らない
    EXPECT_FALSE(NS::SendMsg(MsgPing{}, *sender->m_sensor, sender->m_sensor));
    EXPECT_TRUE(sender->received.empty());
}
