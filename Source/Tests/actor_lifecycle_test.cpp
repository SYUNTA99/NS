#include "Game/Level/PropActor.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/Object/UIActor.h"

#include <gtest/gtest.h>

namespace
{
    class LifeProbeActor final : public NS::Obj::SceneActor
    {
    public:
        int updates = 0;

    protected:
        void StateStep() override { ++updates; }
    };

    class LifeUI final : public NS::Obj::UIActor
    {
    public:
        void OnTick() override { ++updates; }
        int updates = 0;
    };

    // 描画や当たりの部品と同じく、出る時に開始の処理で登録し直す部品
    class RegisterCountSubObj final : public NS::Obj::SubObject
    {
    public:
        void OnAppear() override { OnStart(); }
        void OnStart() override { ++registers; }
        void OnDisappear() noexcept override { ++unregisters; }
        int registers = 0;
        int unregisters = 0;
    };

    class CountedActor final : public NS::Obj::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(CountedActor, NS::Obj::SceneActor)
        NS_REFLECT_SUBOBJ(part, "Counted")
        NS_REFLECT_END()

        std::unique_ptr<RegisterCountSubObj> part;

    protected:
        void OnInit() override { CreateSubObj(part); }
    };
} // namespace

TEST(ActorLifecycle, StartRegistersEachPartOnce)
{
    NS::Obj::Scene scene;
    CountedActor* actor = scene.SpawnTransient<CountedActor>();
    EXPECT_EQ(actor->part->registers, 1);
    EXPECT_EQ(actor->part->unregisters, 0);
}

TEST(ActorLifecycle, ReparentingKeepsRegistrationWhileActivityIsUnchanged)
{
    NS::Obj::Scene scene;
    LifeProbeActor* parent = scene.SpawnTransient<LifeProbeActor>();
    CountedActor* child = scene.SpawnTransient<CountedActor>();
    child->SetParent(parent);
    child->SetParent(nullptr);
    EXPECT_EQ(child->part->registers, 1);
    EXPECT_EQ(child->part->unregisters, 0);

    // 消えている親の下へ入れると外れ、出ている所へ戻すと登録し直す
    child->SetParent(parent);
    parent->Disappear();
    EXPECT_EQ(child->part->unregisters, 1);
    child->SetParent(nullptr);
    EXPECT_EQ(child->part->registers, 2);
}

TEST(ActorLifecycle, DisappearStopsUpdatesAndAppearResumesOnce)
{
    NS::Obj::Scene scene;
    LifeProbeActor* actor = scene.SpawnTransient<LifeProbeActor>();
    EXPECT_TRUE(actor->IsAppeared());
    scene.ExecutePhase(NS::Obj::UpdatePhase::Triggers);
    EXPECT_EQ(actor->updates, 1);
    actor->Disappear();
    actor->Disappear();
    EXPECT_FALSE(actor->IsAppeared());
    scene.ExecutePhase(NS::Obj::UpdatePhase::Triggers);
    EXPECT_EQ(actor->updates, 1);
    actor->Appear();
    actor->Appear();
    scene.ExecutePhase(NS::Obj::UpdatePhase::Triggers);
    EXPECT_EQ(actor->updates, 2);
}

TEST(ActorLifecycle, DestroyedPlacedActorLeavesAfterTheStep)
{
    NS::Obj::Scene scene;
    const std::size_t before = scene.Objects().ObjectCount();
    NS::Obj::SceneActor* actor = scene.SpawnObject<LifeProbeActor>("Probe");
    actor->Destroy();
    EXPECT_FALSE(actor->IsAppeared());
    // 段を回している途中の呼び手がまだ指しているので、その場では解放しない
    EXPECT_EQ(scene.Objects().ObjectCount(), before + 1);
    scene.OnUpdate();
    EXPECT_EQ(scene.Objects().ObjectCount(), before);
}

TEST(ActorLifecycle, DisappearedTransientStaysAfterTheStep)
{
    NS::Obj::Scene scene;
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
    NS::Obj::Scene scene;
    (void)scene.SpawnTransient<LifeProbeActor>();
    ASSERT_EQ(scene.Objects().ObjectCount(), 1u);
    scene.ResetFromJson(NS::Obj::MakeSceneJson());
    EXPECT_EQ(scene.Objects().ObjectCount(), 0u);
}

TEST(ActorLifecycle, PhysicsAndSensorsLeaveTheirRegistries)
{
    NS::Obj::Scene scene;
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

TEST(ActorLifecycle, UICloseStopsTheUpdateAndOpenResumesIt)
{
    NS::Obj::Scene scene;
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

TEST(ActorLifecycle, ParentDisappearRemovesChildPhysicsWithoutChangingChildState)
{
    NS::Obj::Scene scene;
    LifeProbeActor* parent = scene.SpawnTransient<LifeProbeActor>();
    GL::Level::PropActor* child = scene.SpawnTransient<GL::Level::PropActor>();
    child->SetParent(parent);
    scene.SyncPhysics();
    parent->Disappear();
    EXPECT_TRUE(child->IsAppeared());
    EXPECT_TRUE(scene.HitSensors().Sensors().empty());
    float distance = 0.0f;
    EXPECT_FALSE(scene.Physics().Raycast(NS::Vector3{0.0f, 2.0f, 0.0f}, -NS::Vector3::UnitY, 4.0f, distance));
    parent->Appear();
    EXPECT_FALSE(scene.HitSensors().Sensors().empty());
    EXPECT_TRUE(scene.Physics().Raycast(NS::Vector3{0.0f, 2.0f, 0.0f}, -NS::Vector3::UnitY, 4.0f, distance));
}
