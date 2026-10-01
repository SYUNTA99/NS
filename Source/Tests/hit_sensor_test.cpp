#include "Runtime/Core/OBB.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Message.h"
#include "Runtime/Object/Scene/HitSensorDirector.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Physics/Capsule.h"

#include <gtest/gtest.h>

#include <vector>

// ヒットセンサーの形の重なり・組み合わせの表・調べ役の呼び出しと、知らせの送り方を縛る

namespace
{
    using NS::Core::Vector3;

    // 重なった相手を控える試しの Actor
    class SensorProbe final : public NS::Obj::Actor
    {
    public:
        SensorProbe(NS::Obj::HitSensorType type, float radius)
        {
            CreatePart("BodySensor");
            m_sensor = BodySensorPart();
            m_sensor->SetType(type);
            m_sensor->SetSphere(radius);
        }

        void AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other) override
        {
            (void)self;
            touched.push_back(&other);
        }

        bool ReceiveMsg(const NS::Obj::Message& msg, NS::Obj::HitSensor* sender, NS::Obj::HitSensor* receiver) override
        {
            (void)sender;
            (void)receiver;
            received.push_back(msg.Kind());
            return acceptMessages;
        }

        NS::Obj::HitSensor* m_sensor = nullptr;
        std::vector<NS::Obj::HitSensor*> touched;
        std::vector<const void*> received;
        bool acceptMessages = true;
    };

    class MsgPing final : public NS::Obj::Message
    {
        NS_MESSAGE(MsgPing)
    };

    class MsgPong final : public NS::Obj::Message
    {
        NS_MESSAGE(MsgPong)
    };
} // namespace

TEST(SensorVolume, SpheresTouchAtSumOfRadii)
{
    const NS::Obj::SensorVolume a = NS::Obj::SensorVolume::Sphere(Vector3{0.0f, 0.0f, 0.0f}, 1.0f);
    EXPECT_TRUE(NS::Obj::VolumesOverlap(a, NS::Obj::SensorVolume::Sphere(Vector3{1.5f, 0.0f, 0.0f}, 0.5f)));
    EXPECT_FALSE(NS::Obj::VolumesOverlap(a, NS::Obj::SensorVolume::Sphere(Vector3{1.6f, 0.0f, 0.0f}, 0.5f)));
}

TEST(SensorVolume, CapsuleUsesItsSegment)
{
    // 縦に伸びたカプセルは、中心から離れた高さの球にも触れる
    const NS::Obj::SensorVolume capsule = NS::Obj::SensorVolume::Capsule(NS::Phys::Capsule{
        .center = Vector3{0.0f, 0.0f, 0.0f}, .axis = Vector3::UnitY, .halfHeight = 2.0f, .radius = 0.5f});
    EXPECT_TRUE(NS::Obj::VolumesOverlap(capsule, NS::Obj::SensorVolume::Sphere(Vector3{0.8f, 1.9f, 0.0f}, 0.4f)));
    EXPECT_FALSE(NS::Obj::VolumesOverlap(capsule, NS::Obj::SensorVolume::Sphere(Vector3{0.8f, 3.0f, 0.0f}, 0.2f)));
}

TEST(SensorVolume, BoxAgainstSphereAndCapsule)
{
    const NS::Obj::SensorVolume box = NS::Obj::SensorVolume::Box(
        NS::Core::MakeOBB(Vector3{0.0f, 0.0f, 0.0f}, NS::Core::Quaternion::Identity, Vector3{10.0f, 1.0f, 10.0f}));
    EXPECT_TRUE(NS::Obj::VolumesOverlap(box, NS::Obj::SensorVolume::Sphere(Vector3{3.0f, 1.4f, 3.0f}, 0.5f)));
    EXPECT_FALSE(NS::Obj::VolumesOverlap(box, NS::Obj::SensorVolume::Sphere(Vector3{3.0f, 1.6f, 3.0f}, 0.5f)));
    const NS::Obj::SensorVolume capsule = NS::Obj::SensorVolume::Capsule(NS::Phys::Capsule{
        .center = Vector3{0.0f, 2.2f, 0.0f}, .axis = Vector3::UnitY, .halfHeight = 0.5f, .radius = 0.8f});
    EXPECT_TRUE(NS::Obj::VolumesOverlap(capsule, box));
}

TEST(SensorVolume, BoxesUseSeparatingAxes)
{
    const NS::Obj::SensorVolume a = NS::Obj::SensorVolume::Box(
        NS::Core::MakeOBB(Vector3{0.0f, 0.0f, 0.0f}, NS::Core::Quaternion::Identity, Vector3{1.0f, 1.0f, 1.0f}));
    // 45 度回した箱は、角が軸並行の外接箱より内側にある
    const NS::Core::Quaternion turned =
        NS::Core::Quaternion::CreateFromYawPitchRoll(NS::Core::k_Pi * 0.25f, 0.0f, 0.0f);
    EXPECT_TRUE(NS::Obj::VolumesOverlap(
        a,
        NS::Obj::SensorVolume::Box(NS::Core::MakeOBB(Vector3{2.3f, 0.0f, 0.0f}, turned, Vector3{1.0f, 1.0f, 1.0f}))));
    EXPECT_FALSE(NS::Obj::VolumesOverlap(
        a,
        NS::Obj::SensorVolume::Box(NS::Core::MakeOBB(Vector3{2.5f, 0.0f, 0.0f}, turned, Vector3{1.0f, 1.0f, 1.0f}))));
}

TEST(HitSensor, WorldVolumeFollowsRootScale)
{
    NS::Obj::Actor actor;
    actor.CreatePart("BodySensor");
    NS::Obj::HitSensor* sensor = actor.BodySensorPart();
    sensor->SetSphere(0.5f);
    actor.Root().SetPosition(Vector3{1.0f, 2.0f, 3.0f});
    actor.Root().SetScale(Vector3{2.0f, 2.0f, 2.0f});
    const NS::Obj::SensorVolume volume = sensor->WorldVolume();
    EXPECT_FLOAT_EQ(volume.radius, 1.0f);
    EXPECT_FLOAT_EQ(volume.Center().y, 2.0f);
}

TEST(HitSensorDirector, PairTableMatchesThePlan)
{
    using NS::Obj::HitSensorDirector;
    using NS::Obj::HitSensorType;
    EXPECT_TRUE(HitSensorDirector::Checks(HitSensorType::Area, HitSensorType::PlayerBody));
    EXPECT_TRUE(HitSensorDirector::Checks(HitSensorType::PlayerAttack, HitSensorType::MapObjBody));
    EXPECT_FALSE(HitSensorDirector::Checks(HitSensorType::PlayerBody, HitSensorType::Area));
    EXPECT_FALSE(HitSensorDirector::Checks(HitSensorType::MapObjBody, HitSensorType::PlayerAttack));
    EXPECT_FALSE(HitSensorDirector::Checks(HitSensorType::Area, HitSensorType::MapObjBody));
}

TEST(HitSensorDirector, TickCallsAttackSensorOnlyForCheckedPairs)
{
    NS::Obj::Scene scene;
    SensorProbe* area = scene.SpawnTransient<SensorProbe>(NS::Obj::HitSensorType::Area, 1.0f);
    SensorProbe* body = scene.SpawnTransient<SensorProbe>(NS::Obj::HitSensorType::PlayerBody, 0.5f);
    SensorProbe* rock = scene.SpawnTransient<SensorProbe>(NS::Obj::HitSensorType::MapObjBody, 0.5f);
    body->Root().SetPosition(Vector3{1.0f, 0.0f, 0.0f});
    rock->Root().SetPosition(Vector3{0.5f, 0.0f, 0.0f});

    scene.HitSensors().OnTick();

    // 範囲はプレイヤーの体だけを調べる。物の体とプレイヤーの体は自分からは調べない
    ASSERT_EQ(area->touched.size(), 1u);
    EXPECT_EQ(area->touched[0], body->m_sensor);
    EXPECT_TRUE(body->touched.empty());
    EXPECT_TRUE(rock->touched.empty());

    // 離れると呼ばれない
    area->touched.clear();
    body->Root().SetPosition(Vector3{5.0f, 0.0f, 0.0f});
    scene.HitSensors().OnTick();
    EXPECT_TRUE(area->touched.empty());
}

TEST(HitSensorDirector, InvalidSensorIsNotChecked)
{
    NS::Obj::Scene scene;
    SensorProbe* area = scene.SpawnTransient<SensorProbe>(NS::Obj::HitSensorType::Area, 1.0f);
    SensorProbe* body = scene.SpawnTransient<SensorProbe>(NS::Obj::HitSensorType::PlayerBody, 0.5f);
    body->m_sensor->Invalidate();
    scene.HitSensors().OnTick();
    EXPECT_TRUE(area->touched.empty());
}

TEST(HitSensorDirector, FindOverlapsUsesAttackerTypeAndIgnoresSelf)
{
    NS::Obj::Scene scene;
    SensorProbe* rock = scene.SpawnTransient<SensorProbe>(NS::Obj::HitSensorType::MapObjBody, 0.5f);
    SensorProbe* area = scene.SpawnTransient<SensorProbe>(NS::Obj::HitSensorType::Area, 0.5f);
    (void)area;
    const NS::Obj::SensorVolume probe = NS::Obj::SensorVolume::Sphere(Vector3{0.0f, 0.0f, 0.0f}, 0.5f);

    const std::vector<NS::Obj::HitSensor*> found =
        scene.HitSensors().FindOverlaps(probe, NS::Obj::HitSensorType::PlayerAttack, nullptr);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0], rock->m_sensor);
    EXPECT_TRUE(scene.HitSensors().FindOverlaps(probe, NS::Obj::HitSensorType::PlayerAttack, rock).empty());
}

TEST(HitSensorDirector, SensorLeavesDirectorWhenDestroyed)
{
    NS::Obj::Scene scene;
    const std::size_t before = scene.HitSensors().Sensors().size();
    SensorProbe* rock = scene.SpawnTransient<SensorProbe>(NS::Obj::HitSensorType::MapObjBody, 0.5f);
    EXPECT_EQ(scene.HitSensors().Sensors().size(), before + 1);
    rock->OnEndPlay();
    EXPECT_EQ(scene.HitSensors().Sensors().size(), before);
}

TEST(Message, KindsAreDistinctPerType)
{
    const MsgPing ping;
    const MsgPong pong;
    EXPECT_TRUE(NS::Obj::IsMsg<MsgPing>(ping));
    EXPECT_FALSE(NS::Obj::IsMsg<MsgPing>(pong));
    EXPECT_NE(NS::Obj::MsgCast<MsgPong>(pong), nullptr);
    EXPECT_EQ(NS::Obj::MsgCast<MsgPong>(ping), nullptr);
}

TEST(Message, SendReachesReceiverOwnerAndReturnsItsAnswer)
{
    NS::Obj::Scene scene;
    SensorProbe* sender = scene.SpawnTransient<SensorProbe>(NS::Obj::HitSensorType::Area, 1.0f);
    SensorProbe* receiver = scene.SpawnTransient<SensorProbe>(NS::Obj::HitSensorType::PlayerBody, 0.5f);

    EXPECT_TRUE(NS::Obj::SendMsg(MsgPing{}, *receiver->m_sensor, sender->m_sensor));
    ASSERT_EQ(receiver->received.size(), 1u);
    EXPECT_EQ(receiver->received[0], MsgPing::StaticKind());

    // 受け手が応じなければ false
    receiver->acceptMessages = false;
    EXPECT_FALSE(NS::Obj::SendMsg(MsgPong{}, *receiver->m_sensor, sender->m_sensor));

    // 自分宛ては送らない
    EXPECT_FALSE(NS::Obj::SendMsg(MsgPing{}, *sender->m_sensor, sender->m_sensor));
    EXPECT_TRUE(sender->received.empty());
}
