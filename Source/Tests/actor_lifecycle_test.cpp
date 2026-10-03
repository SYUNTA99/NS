#include "Game/Level/MapObj.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/UIActor.h"

#include <gtest/gtest.h>

namespace
{
    class LifeProbe final : public NS::Obj::Actor
    {
    public:
        int updates = 0;

    protected:
        void StateStep() override { ++updates; }
    };

    class LifeUI final : public NS::Obj::UIActor
    {
    public:
        void OnUpdate() override { ++updates; }
        int updates = 0;
    };

    // 描画や当たりの部品と同じく、出る時に開始の処理で登録し直す部品
    class RegisterCountPart final : public NS::Obj::Component
    {
    public:
        void OnAppear() override { OnStart(); }
        void OnStart() override { ++registers; }
        void OnKill() noexcept override { ++unregisters; }
        int registers = 0;
        int unregisters = 0;
    };

    class CountedActor final : public NS::Obj::Actor
    {
    public:
        CountedActor() { AttachFixedComponent(part); }
        void ForEachPart(const PartVisitor& visitor) const override
        {
            NS::Obj::Actor::ForEachPart(visitor);
            visitor("Counted", part);
        }
        mutable RegisterCountPart part;
    };
} // namespace

TEST(ActorLifecycle, StartRegistersEachPartOnce)
{
    NS::Obj::Scene scene;
    CountedActor* actor = scene.SpawnTransient<CountedActor>();
    EXPECT_EQ(actor->part.registers, 1);
    EXPECT_EQ(actor->part.unregisters, 0);
}

TEST(ActorLifecycle, ReparentingKeepsRegistrationWhileActivityIsUnchanged)
{
    NS::Obj::Scene scene;
    LifeProbe* parent = scene.SpawnTransient<LifeProbe>();
    CountedActor* child = scene.SpawnTransient<CountedActor>();
    child->SetParent(parent);
    child->SetParent(nullptr);
    EXPECT_EQ(child->part.registers, 1);
    EXPECT_EQ(child->part.unregisters, 0);

    // 消えている親の下へ入れると外れ、出ている所へ戻すと登録し直す
    child->SetParent(parent);
    parent->Kill();
    EXPECT_EQ(child->part.unregisters, 1);
    child->SetParent(nullptr);
    EXPECT_EQ(child->part.registers, 2);
}

TEST(ActorLifecycle, KillStopsUpdatesAndAppearResumesOnce)
{
    NS::Obj::Scene scene;
    LifeProbe* actor = scene.SpawnTransient<LifeProbe>();
    EXPECT_TRUE(actor->IsAlive());
    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Triggers);
    EXPECT_EQ(actor->updates, 1);
    actor->Kill();
    actor->Kill();
    EXPECT_FALSE(actor->IsAlive());
    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Triggers);
    EXPECT_EQ(actor->updates, 1);
    actor->Appear();
    actor->Appear();
    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Triggers);
    EXPECT_EQ(actor->updates, 2);
}

TEST(ActorLifecycle, PhysicsAndSensorsLeaveTheirRegistries)
{
    NS::Obj::Scene scene;
    NS::Game::Level::MapObj* actor = scene.SpawnTransient<NS::Game::Level::MapObj>();
    scene.SyncPhysics();
    const std::size_t sensors = scene.HitSensors().Sensors().size();
    ASSERT_GT(sensors, 0u);
    float distance = 0.0f;
    const NS::Core::Vector3 from{0.0f, 2.0f, 0.0f};
    ASSERT_TRUE(scene.Physics().Raycast(from, -NS::Core::Vector3::UnitY, 4.0f, distance));
    actor->Kill();
    EXPECT_TRUE(scene.HitSensors().Sensors().empty());
    EXPECT_FALSE(scene.Physics().Raycast(from, -NS::Core::Vector3::UnitY, 4.0f, distance));
    actor->Appear();
    actor->Appear();
    EXPECT_EQ(scene.HitSensors().Sensors().size(), sensors);
    EXPECT_TRUE(scene.Physics().Raycast(from, -NS::Core::Vector3::UnitY, 4.0f, distance));
}

TEST(ActorLifecycle, UIKillAndAppearKeepTheSceneAttachment)
{
    NS::Obj::Scene scene;
    LifeUI ui;
    ui.Open(scene);
    scene.OnUpdate();
    EXPECT_EQ(ui.updates, 1);
    ui.Kill();
    EXPECT_FALSE(ui.IsOpen());
    EXPECT_EQ(ui.OwningScene(), &scene);
    scene.OnUpdate();
    EXPECT_EQ(ui.updates, 1);
    ui.Appear();
    scene.OnUpdate();
    EXPECT_EQ(ui.updates, 2);
    ui.Close();
    EXPECT_EQ(ui.OwningScene(), nullptr);
}

TEST(ActorLifecycle, ParentKillRemovesChildPhysicsWithoutChangingChildLife)
{
    NS::Obj::Scene scene;
    LifeProbe* parent = scene.SpawnTransient<LifeProbe>();
    NS::Game::Level::MapObj* child = scene.SpawnTransient<NS::Game::Level::MapObj>();
    child->SetParent(parent);
    scene.SyncPhysics();
    parent->Kill();
    EXPECT_TRUE(child->IsAlive());
    EXPECT_TRUE(scene.HitSensors().Sensors().empty());
    float distance = 0.0f;
    EXPECT_FALSE(
        scene.Physics().Raycast(NS::Core::Vector3{0.0f, 2.0f, 0.0f}, -NS::Core::Vector3::UnitY, 4.0f, distance));
    parent->Appear();
    EXPECT_FALSE(scene.HitSensors().Sensors().empty());
    EXPECT_TRUE(
        scene.Physics().Raycast(NS::Core::Vector3{0.0f, 2.0f, 0.0f}, -NS::Core::Vector3::UnitY, 4.0f, distance));
}
