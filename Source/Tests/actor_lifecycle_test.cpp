#include "Game/Level/PropActor.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/UIActor.h"

#include <gtest/gtest.h>

namespace
{
    class LifeProbeActor final : public NS::SceneActor
    {
    public:
        int updates = 0;

    protected:
        void StateStep() override { ++updates; }
    };

    class LifeUI final : public NS::UIActor
    {
    public:
        void OnTick() override { ++updates; }
        int updates = 0;
    };

} // namespace

// 置物は模型の本体と残像・影の 3 つを描く物に、体のセンサー 1 つをセンサーに入れる
TEST(ActorLifecycle, StartRegistersEachPartOnce)
{
    NS::Scene scene;
    const std::size_t renderables = scene.RenderableCount();
    (void)scene.SpawnTransient<GL::Level::PropActor>();
    EXPECT_EQ(scene.RenderableCount(), renderables + 3);
    EXPECT_EQ(scene.HitSensors().Sensors().size(), 1u);
}

TEST(ActorLifecycle, DisappearStopsUpdatesAndAppearResumesOnce)
{
    NS::Scene scene;
    LifeProbeActor* actor = scene.SpawnTransient<LifeProbeActor>();
    LifeProbeActor* other = scene.SpawnTransient<LifeProbeActor>();
    EXPECT_TRUE(actor->IsAppeared());
    scene.ExecutePhase(NS::UpdatePhase::Triggers);
    EXPECT_EQ(actor->updates, 1);
    EXPECT_EQ(other->updates, 1);
    actor->Disappear();
    actor->Disappear();
    EXPECT_FALSE(actor->IsAppeared());
    scene.ExecutePhase(NS::UpdatePhase::Triggers);
    EXPECT_EQ(actor->updates, 1);
    EXPECT_EQ(other->updates, 2);
    actor->Appear();
    actor->Appear();
    scene.ExecutePhase(NS::UpdatePhase::Triggers);
    EXPECT_EQ(actor->updates, 2);
    EXPECT_EQ(other->updates, 3);
}

TEST(ActorLifecycle, DestroyedPlacedActorLeavesAfterTheStep)
{
    NS::Scene scene;
    const std::size_t before = scene.Objects().ObjectCount();
    NS::SceneActor* actor = scene.SpawnObject<LifeProbeActor>("Probe");
    actor->Destroy();
    EXPECT_FALSE(actor->IsAppeared());
    // 段を回している途中の呼び手がまだ指しているので、その場では解放しない
    EXPECT_EQ(scene.Objects().ObjectCount(), before + 1);
    scene.OnUpdate();
    EXPECT_EQ(scene.Objects().ObjectCount(), before);
}

TEST(ActorLifecycle, DisappearedTransientStaysAfterTheStep)
{
    NS::Scene scene;
    const std::size_t before = scene.Objects().ObjectCount();
    LifeProbeActor* actor = scene.SpawnTransient<LifeProbeActor>();
    actor->Disappear();
    scene.OnUpdate();
    ASSERT_EQ(scene.Objects().ObjectCount(), before + 1);
    actor->Appear();
    EXPECT_TRUE(actor->IsAppeared());
}

TEST(ActorLifecycle, RebuildingFromJsonDropsTransients)
{
    NS::Scene scene;
    (void)scene.SpawnTransient<LifeProbeActor>();
    ASSERT_EQ(scene.Objects().ObjectCount(), 1u);
    scene.ResetFromJson(NS::MakeSceneJson());
    EXPECT_EQ(scene.Objects().ObjectCount(), 0u);
}

TEST(ActorLifecycle, PhysicsAndSensorsLeaveTheirRegistries)
{
    NS::Scene scene;
    GL::Level::PropActor* actor = scene.SpawnTransient<GL::Level::PropActor>();
    scene.SyncPhysics();
    const std::size_t sensors = scene.HitSensors().Sensors().size();
    ASSERT_GT(sensors, 0u);
    float distance = 0.0f;
    const NS::Vector3 from{0.0f, 2.0f, 0.0f};
    ASSERT_TRUE(scene.Physics().Raycast(from, -NS::Vector3::UnitY, 4.0f, distance));
    actor->Disappear();
    EXPECT_TRUE(scene.HitSensors().Sensors().empty());
    EXPECT_FALSE(scene.Physics().Raycast(from, -NS::Vector3::UnitY, 4.0f, distance));
    actor->Appear();
    actor->Appear();
    EXPECT_EQ(scene.HitSensors().Sensors().size(), sensors);
    EXPECT_TRUE(scene.Physics().Raycast(from, -NS::Vector3::UnitY, 4.0f, distance));
}

TEST(ActorLifecycle, DisappearingOneActorKeepsTheOtherRegistered)
{
    NS::Scene scene;
    GL::Level::PropActor* actor = scene.SpawnTransient<GL::Level::PropActor>();
    GL::Level::PropActor* other = scene.SpawnTransient<GL::Level::PropActor>();
    other->Root().SetPosition({10.0f, 0.0f, 0.0f});
    scene.SyncPhysics();
    const std::size_t renderables = scene.RenderableCount();
    const std::size_t sensors = scene.HitSensors().Sensors().size();
    const NS::uint bodies = scene.Physics().BodyCount();

    actor->Disappear();
    EXPECT_TRUE(other->IsAppeared());
    EXPECT_EQ(scene.RenderableCount(), renderables - 3);
    EXPECT_EQ(scene.HitSensors().Sensors().size(), sensors - 1);
    EXPECT_EQ(scene.Physics().BodyCount(), bodies - 1);
    float distance = 0.0f;
    EXPECT_FALSE(scene.Physics().Raycast({0.0f, 2.0f, 0.0f}, -NS::Vector3::UnitY, 4.0f, distance));
    EXPECT_TRUE(scene.Physics().Raycast({10.0f, 2.0f, 0.0f}, -NS::Vector3::UnitY, 4.0f, distance));

    actor->Appear();
    EXPECT_EQ(scene.RenderableCount(), renderables);
    EXPECT_EQ(scene.HitSensors().Sensors().size(), sensors);
    EXPECT_EQ(scene.Physics().BodyCount(), bodies);
}

TEST(ActorLifecycle, UICloseStopsTheUpdateAndOpenResumesIt)
{
    NS::Scene scene;
    LifeUI ui;
    ui.Open(scene);
    scene.OnUpdate();
    EXPECT_EQ(ui.updates, 1);
    ui.Close();
    EXPECT_FALSE(ui.IsOpen());
    EXPECT_EQ(ui.OwningScene(), nullptr);
    scene.OnUpdate();
    EXPECT_EQ(ui.updates, 1);
    ui.Open(scene);
    EXPECT_EQ(ui.OwningScene(), &scene);
    scene.OnUpdate();
    EXPECT_EQ(ui.updates, 2);
    ui.Close();
    EXPECT_EQ(ui.OwningScene(), nullptr);
}
