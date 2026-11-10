#include "Game/Level/SensorKinds.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/HitSensorsObj.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/StateMachineOf.h"
#include "TestSpawn.h"

#include <gtest/gtest.h>

#include <vector>

// シーンの止めの効き方を見る。止めは頼まれた歩では効かず、次の歩から頼んだ歩数だけ効く。
// 止まっている間も全部のアクターの Update が呼ばれ、止まったアクターは状態機械と BodyStep を進めず、
// VisualStep を進める。止まったかは SceneActor::IsStopped() の 1 つで決まる。
// 物理の管理役は broadphase の木を組み直すだけで効果が見えないので、歩が最後まで進んだ事は、
// 物理の後に回るセンサーの判定で見る

namespace
{
    constexpr int k_StopSteps = 3;

    class StopProbeActor;

    // 進めるたびに持ち主の状態機械の歩数を 1 つ増やす
    class CountingState final : public NS::StateOf<CountingState, StopProbeActor>
    {
    public:
        void OnStep(StopProbeActor* owner, float) override;
    };

    // 歩ごとに、自分が止まっていたかと、状態機械・体・見た目のどれを進めたかを数える。
    // 原点の球のセンサーを持ち、ほかの StopProbeActor と重なり続ける
    class StopProbeActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_NONE(StopProbeActor, NS::SceneActor)

        void OnSensorStay(NS::HitSensor* self, NS::HitSensor* other) override
        {
            (void)self;
            (void)other;
            ++stays;
        }

        // 正なら次の DecideStep で、この長さだけシーンを止めるよう頼む
        int stopRequestSteps = 0;
        // Update ごとに、DecideStep の時の IsStopped() を控える
        std::vector<bool> stoppedPerUpdate;
        int states = 0;
        int bodies = 0;
        int visuals = 0;
        int stays = 0;

    protected:
        void OnInit() override
        {
            NS::StateMachineOf<StopProbeActor>* machine = nullptr;
            (void)BuildStateMachine<StopProbeActor, CountingState>(this, machine);
            CreateClassObj(m_hitSensors);
            NS::HitSensor* body = m_hitSensors->AddShape(this, "Body", GL::Level::SensorKind::Area, NS::HitSensorShape::Sphere);
            NS::Cast<NS::ShapeHitSensor>(body)->SetSphere(1.0f);
        }
        void DecideStep() override
        {
            stoppedPerUpdate.push_back(IsStopped());
            if (stopRequestSteps > 0)
            {
                OwningScene()->RequestStopScene(this, stopRequestSteps);
                stopRequestSteps = 0;
            }
        }
        void BodyStep() override { ++bodies; }
        void VisualStep() override { ++visuals; }
    };

    void CountingState::OnStep(StopProbeActor* owner, float)
    {
        NS_ASSERT(Game, owner != nullptr, "状態の持ち主が無い");
        ++owner->states;
    }

    // 1 歩の前と後で控える、アクターごとの数
    struct ProbeCounts
    {
        int updates = 0;
        int states = 0;
        int bodies = 0;
        int visuals = 0;
        int stays = 0;
    };

    [[nodiscard]] ProbeCounts CountsOf(const StopProbeActor& actor)
    {
        return ProbeCounts{.updates = static_cast<int>(actor.stoppedPerUpdate.size()),
                           .states = actor.states,
                           .bodies = actor.bodies,
                           .visuals = actor.visuals,
                           .stays = actor.stays};
    }

    // 止め始めるアクターを一覧の先頭に、止められるアクター 2 つをその後に置いたシーン。
    // センサーの重なりが始まった歩を過ぎるまで普段の歩で回してある
    struct StopWorld
    {
        StopWorld()
        {
            mover = SpawnFromActorFile<StopProbeActor>(scene, "StopProbeActor", "Mover");
            first = SpawnFromActorFile<StopProbeActor>(scene, "StopProbeActor", "First");
            second = SpawnFromActorFile<StopProbeActor>(scene, "StopProbeActor", "Second");
            for (int i = 0; i < 3; ++i)
            {
                scene.OnUpdate();
            }
        }

        NS::Scene scene;
        StopProbeActor* mover = nullptr;
        StopProbeActor* first = nullptr;
        StopProbeActor* second = nullptr;
    };
} // namespace

NS_CLASS(StopProbeActor)

// 止めを頼んだ歩は最後まで進む。次の歩から頼んだ歩数だけ止まり、その次の歩で戻る
TEST(SceneStop, ARequestStopsTheSceneFromTheNextStepForTheRequestedSteps)
{
    StopWorld world;
    ASSERT_NE(world.mover, nullptr);
    ASSERT_NE(world.first, nullptr);
    ASSERT_NE(world.second, nullptr);
    ASSERT_GT(world.first->stays, 0);
    world.mover->stopRequestSteps = k_StopSteps;

    // 頼んだ歩は、一覧で止め始めたアクターより後のアクターも状態機械を進め、センサーの判定も回る
    ProbeCounts before = CountsOf(*world.first);
    world.scene.OnUpdate();
    ProbeCounts after = CountsOf(*world.first);
    EXPECT_FALSE(world.first->stoppedPerUpdate.back());
    EXPECT_EQ(after.states - before.states, 1);
    EXPECT_GT(after.stays, before.stays);

    // 次の歩から頼んだ歩数だけ止まる。止まっている歩はセンサーの判定が回らない
    for (int step = 0; step < k_StopSteps; ++step)
    {
        before = CountsOf(*world.first);
        world.scene.OnUpdate();
        after = CountsOf(*world.first);
        EXPECT_TRUE(world.first->stoppedPerUpdate.back()) << step;
        EXPECT_EQ(after.states, before.states) << step;
        EXPECT_EQ(after.stays, before.stays) << step;
    }

    // 止めが終わった歩は普段どおり進む
    before = CountsOf(*world.first);
    world.scene.OnUpdate();
    after = CountsOf(*world.first);
    EXPECT_FALSE(world.first->stoppedPerUpdate.back());
    EXPECT_EQ(after.states - before.states, 1);
    EXPECT_GT(after.stays, before.stays);
}

// シーンが止まっている歩も全部のアクターの Update が呼ばれる。止め始めたアクターは状態機械と体を進め、
// ほかのアクターは状態機械と体を進めずに見た目だけを進める
TEST(SceneStop, EveryActorUpdatesAndOnlyTheMoverAdvancesItsMachineAndBody)
{
    StopWorld world;
    ASSERT_NE(world.mover, nullptr);
    ASSERT_NE(world.first, nullptr);
    ASSERT_NE(world.second, nullptr);
    world.mover->stopRequestSteps = k_StopSteps;
    world.scene.OnUpdate();

    const ProbeCounts moverBefore = CountsOf(*world.mover);
    const ProbeCounts firstBefore = CountsOf(*world.first);
    const ProbeCounts secondBefore = CountsOf(*world.second);
    for (int step = 0; step < k_StopSteps; ++step)
    {
        world.scene.OnUpdate();
    }

    const ProbeCounts moverAfter = CountsOf(*world.mover);
    EXPECT_EQ(moverAfter.updates - moverBefore.updates, k_StopSteps);
    EXPECT_EQ(moverAfter.states - moverBefore.states, k_StopSteps);
    EXPECT_EQ(moverAfter.bodies - moverBefore.bodies, k_StopSteps);
    EXPECT_EQ(moverAfter.visuals - moverBefore.visuals, k_StopSteps);

    const ProbeCounts firstAfter = CountsOf(*world.first);
    EXPECT_EQ(firstAfter.updates - firstBefore.updates, k_StopSteps);
    EXPECT_EQ(firstAfter.states, firstBefore.states);
    EXPECT_EQ(firstAfter.bodies, firstBefore.bodies);
    EXPECT_EQ(firstAfter.visuals - firstBefore.visuals, k_StopSteps);

    const ProbeCounts secondAfter = CountsOf(*world.second);
    EXPECT_EQ(secondAfter.updates - secondBefore.updates, k_StopSteps);
    EXPECT_EQ(secondAfter.states, secondBefore.states);
    EXPECT_EQ(secondAfter.bodies, secondBefore.bodies);
    EXPECT_EQ(secondAfter.visuals - secondBefore.visuals, k_StopSteps);
}

// IsStopped() は自分の当たりの止めでも、シーンの止めでも true になる。止め始めたアクターはシーンの止めでは
// true にならない
TEST(SceneStop, IsStoppedSeesTheOwnHitStopAndTheSceneStopExceptForTheMover)
{
    StopWorld world;
    ASSERT_NE(world.mover, nullptr);
    ASSERT_NE(world.first, nullptr);
    ASSERT_NE(world.second, nullptr);

    EXPECT_FALSE(world.first->IsStopped());
    world.first->RequestHitStop(2);
    EXPECT_TRUE(world.first->IsStopped());
    EXPECT_FALSE(world.second->IsStopped());
    world.scene.OnUpdate();
    world.scene.OnUpdate();
    EXPECT_FALSE(world.first->IsStopped());

    world.mover->stopRequestSteps = k_StopSteps;
    world.scene.OnUpdate();
    world.scene.OnUpdate();
    EXPECT_TRUE(world.scene.IsSceneStopped());
    EXPECT_TRUE(world.scene.IsStopSceneMover(world.mover));
    EXPECT_FALSE(world.scene.IsStopSceneMover(world.first));
    EXPECT_FALSE(world.mover->stoppedPerUpdate.back());
    EXPECT_TRUE(world.first->stoppedPerUpdate.back());
    EXPECT_TRUE(world.second->stoppedPerUpdate.back());

    world.scene.CancelStopScene();
    EXPECT_FALSE(world.scene.IsSceneStopped());
    EXPECT_FALSE(world.first->IsStopped());
}

// 止め始めたアクターは止めの最中だけ。止めが終わった後は、前に止めたアクターも nullptr も止め始めたアクターでない
TEST(SceneStop, NoActorIsTheMoverWhileNothingStopsTheScene)
{
    NS::Scene scene;
    EXPECT_FALSE(scene.IsStopSceneMover(nullptr));

    StopWorld world;
    ASSERT_NE(world.mover, nullptr);
    world.mover->stopRequestSteps = k_StopSteps;
    world.scene.OnUpdate();
    world.scene.OnUpdate();
    ASSERT_TRUE(world.scene.IsStopSceneMover(world.mover));
    world.scene.CancelStopScene();
    EXPECT_FALSE(world.scene.IsStopSceneMover(world.mover));
    EXPECT_FALSE(world.scene.IsStopSceneMover(nullptr));
}

// 止めは先に止め始めたアクターが持つ。止まっている間は、ほかのアクターの依頼は延ばすのも解くのも受けない
TEST(SceneStop, TheFirstMoverKeepsTheStopUntilItEndsOrCancelsIt)
{
    NS::Scene scene;
    const NS::SceneActor* first = SpawnFromActorFile<StopProbeActor>(scene, "StopProbeActor", "First");
    const NS::SceneActor* second = SpawnFromActorFile<StopProbeActor>(scene, "StopProbeActor", "Second");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    scene.RequestStopScene(first, 10);
    scene.OnUpdate();
    ASSERT_TRUE(scene.IsSceneStopped());

    scene.RequestStopScene(second, 20);
    scene.OnUpdate();
    scene.RequestStopScene(second, 0);
    scene.OnUpdate();
    EXPECT_TRUE(scene.IsSceneStopped());
    EXPECT_TRUE(scene.IsStopSceneMover(first));
    EXPECT_FALSE(scene.IsStopSceneMover(second));

    scene.RequestStopScene(first, 0);
    scene.OnUpdate();
    EXPECT_FALSE(scene.IsSceneStopped());
}

// 止め始めたアクターが消えても、止めは残りの歩数で切れる。後から入ったアクターを止め始めたアクターと見なさない
TEST(SceneStop, RemovingTheMoverKeepsTheSceneStoppedUntilTheStopEnds)
{
    NS::Scene scene;
    const NS::SceneActor* mover = SpawnFromActorFile<StopProbeActor>(scene, "StopProbeActor", "Mover");
    ASSERT_NE(mover, nullptr);
    scene.RequestStopScene(mover, 2);
    scene.OnUpdate();
    ASSERT_TRUE(scene.IsSceneStopped());

    scene.RemoveActor(mover->Id());
    const NS::SceneActor* late = SpawnFromActorFile<StopProbeActor>(scene, "StopProbeActor", "Late");
    ASSERT_NE(late, nullptr);
    EXPECT_TRUE(scene.IsSceneStopped());
    EXPECT_FALSE(scene.IsStopSceneMover(late));

    scene.OnUpdate();
    EXPECT_FALSE(scene.IsSceneStopped());
}

// 0 歩の依頼と、プレイを始め直す事は、保存した依頼を捨てる。次の歩でも止まらない
TEST(SceneStop, ZeroStepsAndRestartingThePlayDropTheRequest)
{
    NS::Scene scene;
    const NS::SceneActor* mover = SpawnFromActorFile<StopProbeActor>(scene, "StopProbeActor", "Mover");
    ASSERT_NE(mover, nullptr);
    scene.RequestStopScene(mover, k_StopSteps);
    scene.RequestStopScene(mover, 0);
    scene.OnUpdate();
    EXPECT_FALSE(scene.IsSceneStopped());

    scene.RequestStopScene(mover, k_StopSteps);
    scene.SetSimulationEnabled(true);
    scene.OnUpdate();
    EXPECT_FALSE(scene.IsSceneStopped());

    scene.RequestStopScene(mover, k_StopSteps);
    scene.CancelStopScene();
    scene.OnUpdate();
    EXPECT_FALSE(scene.IsSceneStopped());
}
