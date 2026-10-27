#include "Editor/EditorActors.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Actors/LightActor.h"
#include "NSlib/CoreObject/ClassObjs/BoxCollision.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/UIActor.h"
#include "TestActorFile.h"
#include "TestRegistrations.h"

#include <gtest/gtest.h>

namespace
{
    // 自分では当たりを写さない、置く側の当たりだけを持つ Actor
    class LifeBoxActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(LifeBoxActor, NS::SceneActor)

    protected:
        void OnInit() override { CreateClassObj<NS::Collision, NS::BoxCollision>(m_collision); }
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
        nlohmann::json object = NS::MakeActorJson();
        NS::SetActorJsonId(object, id);
        NS::SetActorJsonFile(object, "LifeBoxActor");
        NS::SetActorJsonName(object, name);
        NS::Editor::SetActorJsonPosition(object, {x, 0.0f, 0.0f});
        return object;
    }

    NS::Vector3 InterpolatedStart(const NS::SceneActor* actor)
    {
        NS_ASSERT(Game, actor != nullptr, "InterpolatedStart: 渡したアクターか部品が無い");
        return actor->Root().InterpolatedWorldMatrix(0.0f).Translation();
    }

    class LifeProbeActor final : public NS::SceneActor
    {
    public:
        int updates = 0;

    protected:
        void DecideStep() override { ++updates; }
    };

    class LifeUI final : public NS::UIActor
    {
    public:
        void OnTick() override { ++updates; }
        int updates = 0;
    };

} // namespace

// 置物は模型の本体と残像・影・跡の 4 つを描く物に、体のセンサー 1 つをセンサーに入れる
TEST(ActorLifecycle, StartRegistersEachPartOnce)
{
    NS::Scene scene;
    const std::size_t renderables = scene.RenderableCount();
    (void)scene.SpawnActor<GL::Level::PropActor>("PropActor");
    EXPECT_EQ(scene.RenderableCount(), renderables + 4);
    EXPECT_EQ(scene.EnabledRenderableCount(), renderables + 4);
    EXPECT_EQ(scene.HitSensors().Sensors().size(), 1u);
    EXPECT_EQ(EnabledSensorCount(scene), 1u);
}

TEST(ActorLifecycle, DisappearStopsUpdatesAndAppearResumesOnce)
{
    NS::Scene scene;
    LifeProbeActor* actor = scene.SpawnActor<LifeProbeActor>("LifeProbeActor");
    LifeProbeActor* other = scene.SpawnActor<LifeProbeActor>("LifeProbeActor");
    EXPECT_TRUE(actor->IsAppeared());
    scene.ExecutePhase(NS::UpdatePhase::Actors);
    EXPECT_EQ(actor->updates, 1);
    EXPECT_EQ(other->updates, 1);
    actor->Disappear();
    actor->Disappear();
    EXPECT_FALSE(actor->IsAppeared());
    scene.ExecutePhase(NS::UpdatePhase::Actors);
    EXPECT_EQ(actor->updates, 1);
    EXPECT_EQ(other->updates, 2);
    actor->Appear();
    actor->Appear();
    scene.ExecutePhase(NS::UpdatePhase::Actors);
    EXPECT_EQ(actor->updates, 2);
    EXPECT_EQ(other->updates, 3);
}

TEST(ActorLifecycle, DestroyedPlacedActorLeavesAfterTheStep)
{
    NS::Scene scene;
    const std::size_t before = scene.Actors().ActorCount();
    NS::SceneActor* actor = scene.SpawnActor<LifeProbeActor>("Probe");
    actor->Destroy();
    EXPECT_FALSE(actor->IsAppeared());
    // 段を回している途中の呼び手がまだ指しているので、その場では解放しない
    EXPECT_EQ(scene.Actors().ActorCount(), before + 1);
    scene.OnUpdate();
    EXPECT_EQ(scene.Actors().ActorCount(), before);
}

TEST(ActorLifecycle, PhysicsAndSensorsStopWhileDisappeared)
{
    NS::Scene scene;
    scene.SetSimulationEnabled(false);
    GL::Level::PropActor* actor = scene.SpawnActor<GL::Level::PropActor>("PropActor");
    scene.OnUpdate();
    const std::size_t sensors = EnabledSensorCount(scene);
    ASSERT_GT(sensors, 0u);
    float distance = 0.0f;
    const NS::Vector3 from{0.0f, 2.0f, 0.0f};
    ASSERT_TRUE(scene.Physics().Raycast(from, -NS::Vector3::UnitY, 4.0f, distance));
    actor->Disappear();
    EXPECT_EQ(EnabledSensorCount(scene), 0u);
    EXPECT_FALSE(scene.Physics().Raycast(from, -NS::Vector3::UnitY, 4.0f, distance));
    actor->Appear();
    actor->Appear();
    EXPECT_EQ(EnabledSensorCount(scene), sensors);
    EXPECT_TRUE(scene.Physics().Raycast(from, -NS::Vector3::UnitY, 4.0f, distance));
}

TEST(ActorLifecycle, DisappearingOneActorKeepsTheOtherRegistered)
{
    NS::Scene scene;
    scene.SetSimulationEnabled(false);
    GL::Level::PropActor* actor = scene.SpawnActor<GL::Level::PropActor>("PropActor");
    GL::Level::PropActor* other = scene.SpawnActor<GL::Level::PropActor>("PropActor");
    other->Root().SetPosition({10.0f, 0.0f, 0.0f});
    scene.OnUpdate();
    const std::size_t renderables = scene.EnabledRenderableCount();
    const std::size_t sensors = EnabledSensorCount(scene);
    const NS::uint bodies = scene.Physics().BodyCount();
    const NS::uint attached = scene.Physics().AttachedBodyCount();

    actor->Disappear();
    EXPECT_TRUE(other->IsAppeared());
    EXPECT_EQ(scene.EnabledRenderableCount(), renderables - 4);
    EXPECT_EQ(EnabledSensorCount(scene), sensors - 1);
    // 当たりの body は物理シーンの外へ出るだけで壊れない
    EXPECT_EQ(scene.Physics().AttachedBodyCount(), attached - 1);
    EXPECT_EQ(scene.Physics().BodyCount(), bodies);
    float distance = 0.0f;
    EXPECT_FALSE(scene.Physics().Raycast({0.0f, 2.0f, 0.0f}, -NS::Vector3::UnitY, 4.0f, distance));
    EXPECT_TRUE(scene.Physics().Raycast({10.0f, 2.0f, 0.0f}, -NS::Vector3::UnitY, 4.0f, distance));

    actor->Appear();
    EXPECT_EQ(scene.EnabledRenderableCount(), renderables);
    EXPECT_EQ(EnabledSensorCount(scene), sensors);
    EXPECT_EQ(scene.Physics().AttachedBodyCount(), attached);
    EXPECT_EQ(scene.Physics().BodyCount(), bodies);
}

// 一方を消してから出し直しても、描く物とセンサーの並びは前と同じ順のまま
TEST(ActorLifecycle, AppearAgainKeepsRegistrationOrder)
{
    NS::Scene scene;
    GL::Level::PropActor* first = scene.SpawnActor<GL::Level::PropActor>("PropActor");
    GL::Level::PropActor* second = scene.SpawnActor<GL::Level::PropActor>("PropActor");
    second->Root().SetPosition({10.0f, 0.0f, 0.0f});
    scene.OnUpdate();
    const std::vector<NS::IRenderable*> renderables = scene.Renderables();
    const std::vector<NS::HitSensor*> sensors = scene.HitSensors().Sensors();
    ASSERT_GE(sensors.size(), 2u);

    first->Disappear();
    first->Appear();
    EXPECT_EQ(scene.Renderables(), renderables);
    EXPECT_EQ(scene.HitSensors().Sensors(), sensors);
}

// 消えたまま一覧から外したアクターの部品は、どの登録簿にも残らない
TEST(ActorLifecycle, DisappearedActorLeavesRegistriesOnEndPlay)
{
    NS::Scene scene;
    // 部品が作るシーンに 1 つの管理役 (当たりの画面の係など) は残るので、作られた後の数と比べる
    (void)scene.SpawnActor<GL::Level::PropActor>("PropActor");
    scene.OnUpdate();
    const Registrations before = CountRegistrations(scene);

    const std::vector<NS::SceneActor*> actors{scene.SpawnActor<GL::Level::PropActor>("PropActor"),
                                              scene.SpawnActor<PlayerActor>("PlayerActor"),
                                              scene.SpawnActor<NS::LightActor>("LightActor"),
                                              scene.SpawnActor<GL::Level::FollowCameraActor>("FollowCameraActor")};
    scene.OnUpdate();
    for (NS::SceneActor* actor : actors)
    {
        actor->Disappear();
    }
    for (NS::SceneActor* actor : actors)
    {
        actor->Destroy();
    }
    scene.OnUpdate();
    EXPECT_EQ(CountRegistrations(scene), before);
}

// どの道で入れても、入れた直後から置く側の当たりがある。呼び手が当たりを張り直さなくてよい
TEST(ActorLifecycle, EveryWayInGivesTheCollisionRightAway)
{
    const ScopedActorFile lifeBox{"LifeBoxActor", CodeValuesActorFile("LifeBoxActor")};
    NS::Scene scene;
    (void)scene.SpawnActor<LifeBoxActor>("LifeBoxActor");
    EXPECT_TRUE(HitsDownAt(scene, 0.0f));

    NS::SceneActor* placed = scene.SpawnActor<LifeBoxActor>("Placed");
    placed->Root().SetPosition({10.0f, 0.0f, 0.0f});
    scene.OnUpdate();
    EXPECT_TRUE(HitsDownAt(scene, 10.0f));

    ASSERT_NE(scene.SpawnFromJson(LifeBoxJson(50, "FromJson", 20.0f)), nullptr);
    EXPECT_TRUE(HitsDownAt(scene, 20.0f));
}

// id の無い JSON から入れたアクターは id が 0 で重なる。止め始めたアクターだけを止め始めた物と見る
TEST(ActorLifecycle, AnotherActorWithoutAnIdIsNotTheHolderOfTheHold)
{
    NS::Scene scene;
    NS::SceneActor* holder = scene.SpawnFromJson(LifeBoxJson(0, "Holder", 0.0f));
    NS::SceneActor* other = scene.SpawnFromJson(LifeBoxJson(0, "Other", 5.0f));
    ASSERT_NE(holder, nullptr);
    ASSERT_NE(other, nullptr);
    ASSERT_EQ(holder->Id(), other->Id());

    scene.HoldOthers(holder, 2);

    EXPECT_TRUE(scene.IsOthersHolder(holder));
    EXPECT_FALSE(scene.IsOthersHolder(other));
}

TEST(ActorLifecycle, DisappearRemovesTheCollisionAndAppearBringsItBack)
{
    const ScopedActorFile lifeBox{"LifeBoxActor", CodeValuesActorFile("LifeBoxActor")};
    NS::Scene scene;
    NS::SceneActor* actor = scene.SpawnFromJson(LifeBoxJson(50, "Box", 0.0f));
    ASSERT_NE(actor, nullptr);
    actor->Disappear();
    EXPECT_FALSE(HitsDownAt(scene, 0.0f));
    actor->Appear();
    EXPECT_TRUE(HitsDownAt(scene, 0.0f));
}

// 消える時は当たりの body を物理シーンから外すだけで壊さず、出る時に同じ body を戻す
TEST(ActorLifecycle, DisappearKeepsCollisionBody)
{
    const ScopedActorFile lifeBox{"LifeBoxActor", CodeValuesActorFile("LifeBoxActor")};
    NS::Scene scene;
    NS::SceneActor* actor = scene.SpawnFromJson(LifeBoxJson(50, "Box", 0.0f));
    ASSERT_NE(actor, nullptr);
    const JPH::BodyID body = actor->CollisionClassObj()->BodyId();
    ASSERT_FALSE(body.IsInvalid());
    const NS::uint bodies = scene.Physics().BodyCount();

    actor->Disappear();
    EXPECT_FALSE(HitsDownAt(scene, 0.0f));
    EXPECT_EQ(scene.Physics().BodyCount(), bodies);
    EXPECT_EQ(actor->CollisionClassObj()->BodyId(), body);

    actor->Appear();
    EXPECT_TRUE(HitsDownAt(scene, 0.0f));
    EXPECT_EQ(scene.Physics().BodyCount(), bodies);
    EXPECT_EQ(actor->CollisionClassObj()->BodyId(), body);
}

// 一覧から外したアクターの当たりの body は、出ていても消えていても物理シーンに残らない
TEST(ActorLifecycle, EndPlayDestroysCollisionBody)
{
    const ScopedActorFile lifeBox{"LifeBoxActor", CodeValuesActorFile("LifeBoxActor")};
    NS::Scene scene;
    const NS::uint before = scene.Physics().BodyCount();
    NS::SceneActor* shown = scene.SpawnFromJson(LifeBoxJson(50, "Shown", 0.0f));
    NS::SceneActor* hidden = scene.SpawnFromJson(LifeBoxJson(51, "Hidden", 10.0f));
    ASSERT_NE(shown, nullptr);
    ASSERT_NE(hidden, nullptr);
    ASSERT_EQ(scene.Physics().BodyCount(), before + 2);

    hidden->Disappear();
    shown->Destroy();
    hidden->Destroy();
    scene.OnUpdate();
    EXPECT_EQ(scene.Physics().BodyCount(), before);
    EXPECT_FALSE(HitsDownAt(scene, 0.0f));
}

// 入れた直後の絵は、原点から配置先へ補間しない
TEST(ActorLifecycle, EveryWayInStartsInterpolationAtThePlacement)
{
    const ScopedActorFile lifeBox{"LifeBoxActor", CodeValuesActorFile("LifeBoxActor")};
    NS::Scene scene;
    nlohmann::json sceneJson = NS::MakeSceneJson();
    NS::SceneJsonActors(sceneJson).push_back(LifeBoxJson(10, "Rebuilt", 3.0f));
    scene.ResetFromJson(sceneJson);
    EXPECT_EQ(InterpolatedStart(scene.Actors().FindByActorId(10)), NS::Vector3(3.0f, 0.0f, 0.0f));

    const NS::SceneActor* spawned = scene.SpawnFromJson(LifeBoxJson(20, "Spawned", 4.0f));
    ASSERT_NE(spawned, nullptr);
    EXPECT_EQ(InterpolatedStart(spawned), NS::Vector3(4.0f, 0.0f, 0.0f));

    const NS::SceneActor* applied = ApplySnapshot(scene, LifeBoxJson(30, "Applied", 6.0f));
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(InterpolatedStart(applied), NS::Vector3(6.0f, 0.0f, 0.0f));
}

// 置いた後に動かすと、次の歩で当たりが付いて来る。世界を止めた編集中も同じ
TEST(ActorLifecycle, MovedCollisionFollowsOnTheNextStep)
{
    const ScopedActorFile lifeBox{"LifeBoxActor", CodeValuesActorFile("LifeBoxActor")};
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
