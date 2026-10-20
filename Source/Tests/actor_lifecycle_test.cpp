#include "Editor/EditorObjects.h"
#include "Game/Level/PropActor.h"
#include "NSlib/CoreObject/ClassObjs/BoxCollision.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/UIActor.h"

#include <gtest/gtest.h>

namespace
{
    // 自分では当たりを写さない、置く側の当たりだけを持つ Actor
    class LifeBoxActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(LifeBoxActor, NS::SceneActor)

    protected:
        void OnInit() override { CreateClassObj<NS::Collision, NS::BoxCollision>(m_collision, this); }
    };
} // namespace

NS_PLACEABLE(LifeBoxActor, "試しの箱")

namespace
{
    bool HitsDownAt(const NS::Scene& scene, float x)
    {
        float distance = 0.0f;
        return scene.Physics().Raycast({x, 2.0f, 0.0f}, -NS::Vector3::UnitY, 4.0f, distance);
    }

    nlohmann::json LifeBoxJson(NS::uint32 id, std::string_view name, float x)
    {
        nlohmann::json object = NS::MakeObjectJson();
        NS::SetObjectJsonId(object, id);
        NS::SetObjectJsonClass(object, "LifeBoxActor");
        NS::SetObjectJsonName(object, name);
        NS::Editor::SetObjectPosition(object, {x, 0.0f, 0.0f});
        return object;
    }

    NS::Vector3 InterpolatedStart(const NS::SceneActor& actor)
    {
        return actor.Root().InterpolatedWorldMatrix(0.0f).Translation();
    }

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
    scene.SetSimulationEnabled(false);
    GL::Level::PropActor* actor = scene.SpawnTransient<GL::Level::PropActor>();
    scene.OnUpdate();
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
    scene.SetSimulationEnabled(false);
    GL::Level::PropActor* actor = scene.SpawnTransient<GL::Level::PropActor>();
    GL::Level::PropActor* other = scene.SpawnTransient<GL::Level::PropActor>();
    other->Root().SetPosition({10.0f, 0.0f, 0.0f});
    scene.OnUpdate();
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

// どの道で入れても、入れた直後から置く側の当たりがある。呼び手が当たりを張り直さなくてよい
TEST(ActorLifecycle, EveryWayInGivesTheCollisionRightAway)
{
    NS::Scene scene;
    (void)scene.SpawnTransient<LifeBoxActor>();
    EXPECT_TRUE(HitsDownAt(scene, 0.0f));

    NS::SceneActor* placed = scene.SpawnObject<LifeBoxActor>("Placed");
    placed->Root().SetPosition({10.0f, 0.0f, 0.0f});
    scene.OnUpdate();
    EXPECT_TRUE(HitsDownAt(scene, 10.0f));

    ASSERT_NE(scene.SpawnFromJson(LifeBoxJson(50, "FromJson", 20.0f)), nullptr);
    EXPECT_TRUE(HitsDownAt(scene, 20.0f));
}

TEST(ActorLifecycle, DisappearRemovesTheCollisionAndAppearBringsItBack)
{
    NS::Scene scene;
    NS::SceneActor* actor = scene.SpawnFromJson(LifeBoxJson(50, "Box", 0.0f));
    ASSERT_NE(actor, nullptr);
    actor->Disappear();
    EXPECT_FALSE(HitsDownAt(scene, 0.0f));
    actor->Appear();
    EXPECT_TRUE(HitsDownAt(scene, 0.0f));
}

// 入れた直後の絵は、原点から配置先へ補間しない
TEST(ActorLifecycle, EveryWayInStartsInterpolationAtThePlacement)
{
    NS::Scene scene;
    nlohmann::json sceneJson = NS::MakeSceneJson();
    NS::SceneJsonObjects(sceneJson).push_back(LifeBoxJson(10, "Rebuilt", 3.0f));
    scene.ResetFromJson(sceneJson);
    EXPECT_EQ(InterpolatedStart(*scene.Objects().FindByObjectId(10)), NS::Vector3(3.0f, 0.0f, 0.0f));

    const NS::SceneActor* spawned = scene.SpawnFromJson(LifeBoxJson(20, "Spawned", 4.0f));
    ASSERT_NE(spawned, nullptr);
    EXPECT_EQ(InterpolatedStart(*spawned), NS::Vector3(4.0f, 0.0f, 0.0f));

    const NS::SceneActor* replaced = scene.ReplaceFromJson(LifeBoxJson(20, "Spawned", 5.0f));
    ASSERT_NE(replaced, nullptr);
    EXPECT_EQ(InterpolatedStart(*replaced), NS::Vector3(5.0f, 0.0f, 0.0f));

    const NS::SceneActor* applied = scene.ApplyFromJson(LifeBoxJson(30, "Applied", 6.0f));
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(InterpolatedStart(*applied), NS::Vector3(6.0f, 0.0f, 0.0f));
}

// 置いた後に動かすと、次の歩で当たりが付いて来る。世界を止めた編集中も同じ
TEST(ActorLifecycle, MovedCollisionFollowsOnTheNextStep)
{
    NS::Scene scene;
    NS::SceneActor* actor = scene.SpawnFromJson(LifeBoxJson(50, "Box", 0.0f));
    ASSERT_NE(actor, nullptr);

    actor->Root().SetPosition({10.0f, 0.0f, 0.0f});
    scene.OnUpdate();
    EXPECT_FALSE(HitsDownAt(scene, 0.0f));
    EXPECT_TRUE(HitsDownAt(scene, 10.0f));

    scene.SetSimulationEnabled(false);
    actor->Root().SetPosition({20.0f, 0.0f, 0.0f});
    scene.OnUpdate();
    EXPECT_FALSE(HitsDownAt(scene, 10.0f));
    EXPECT_TRUE(HitsDownAt(scene, 20.0f));
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
