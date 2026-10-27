#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/StateMachineOf.h"
#include "NSlib/CoreObject/UpdatePhase.h"
#include "NSlib/Core/Windows/Clock.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace
{
    // 自分の Update の中で、控えた id のアクターをシーンから消しに行く
    class DestroyerActor final : public NS::SceneActor
    {
    public:
        explicit DestroyerActor(NS::Scene& scene) : m_scene(scene) {}
        void SetTarget(NS::uint32 objectId) noexcept { m_target = objectId; }

    protected:
        void DecideStep() override { m_scene.RemoveActor(m_target); }

    private:
        NS::Scene& m_scene;
        NS::uint32 m_target = NS::k_NoActorId;
    };

    class CountingActor final : public NS::SceneActor
    {
    public:
        int updates = 0;

    protected:
        void DecideStep() override { ++updates; }
    };

    class DrawModelActor final : public NS::SceneActor
    {
    protected:
        void OnInit() override { CreateClassObj(m_model); }
    };

    // 別のアクターの描く倍率を Update の中で書く。補間の始点が書く前の値で控えられたかを見る
    class DrawScaleWriterActor final : public NS::SceneActor
    {
    public:
        explicit DrawScaleWriterActor(DrawModelActor* target) : m_target(target)
        {
            NS_ASSERT(Game, target != nullptr, "DrawScaleWriterActor: 書く先のアクターが無い");
        }

    protected:
        void DecideStep() override { (void)m_target->ModelClassObj()->SetDrawScale(NS::Vector3{2.0f, 2.0f, 2.0f}); }

    private:
        DrawModelActor* m_target = nullptr;
    };

    class PhaseTicker final : public NS::SceneSystem
    {
    public:
        PhaseTicker(std::vector<std::string>& log, std::string label) : m_log(log), m_label(std::move(label)) {}
        void OnTick() override { m_log.push_back(m_label); }

    private:
        std::vector<std::string>& m_log;
        std::string m_label;
    };

    class CountingTicker final : public NS::SceneSystem
    {
    public:
        void OnTick() override { ++ticks; }
        int ticks = 0;
    };

    // 自分の Update の中で、渡された管理役をアクターの段へ登録する
    class LateRegisterActor final : public NS::SceneActor
    {
    public:
        LateRegisterActor(NS::Scene& scene, NS::SceneSystem& system) : m_scene(scene), m_system(system) {}

    protected:
        void DecideStep() override { m_scene.AddSystemTick(m_system, NS::UpdatePhase::Actors); }

    private:
        NS::Scene& m_scene;
        NS::SceneSystem& m_system;
    };

    // 自分の更新の中で、控えた id のアクターをシーンから消しに行く
    class DestroyerSystem final : public NS::SceneSystem
    {
    public:
        explicit DestroyerSystem(NS::Scene& scene) : m_scene(scene) {}
        void SetTarget(NS::uint32 objectId) noexcept { m_target = objectId; }
        void OnTick() override { m_scene.RemoveActor(m_target); }

    private:
        NS::Scene& m_scene;
        NS::uint32 m_target = NS::k_NoActorId;
    };

    // 更新のたびに名前を、描く支度のたびに名前と ":render-prep" を控える
    class FrameLogActor final : public NS::SceneActor
    {
    public:
        FrameLogActor(std::vector<std::string>& log, std::string label) : m_log(log), m_label(std::move(label)) {}
        void PrepareRender() override { m_log.push_back(m_label + ":render-prep"); }
        // 正なら次の DecideStep で、この長さだけ自分以外を止め始める
        int holdSteps = 0;

    protected:
        void DecideStep() override
        {
            m_log.push_back(m_label);
            if (holdSteps > 0)
            {
                OwningScene()->HoldOthers(this, holdSteps);
                holdSteps = 0;
            }
        }

    private:
        std::vector<std::string>& m_log;
        std::string m_label;
    };

    [[nodiscard]] int CountOf(const std::vector<std::string>& log, const std::string& label)
    {
        return static_cast<int>(std::count(log.begin(), log.end(), label));
    }

    class StepProbeActor;

    // 進めるたびに持ち主の控えへ "state" を書く
    class LoggingState final : public NS::StateOf<LoggingState, StepProbeActor>
    {
    public:
        void OnStep(StepProbeActor* owner, float) override;
    };

    // 上書きできる 4 つの関数と状態機械の 1 歩が呼ばれた順を控える
    class StepProbeActor final : public NS::SceneActor
    {
    public:
        explicit StepProbeActor(std::vector<std::string>& log) : m_log(log) {}
        void Build()
        {
            NS::StateMachineOf<StepProbeActor>* machine = nullptr;
            (void)BuildStateMachine<StepProbeActor, LoggingState>(this, machine);
        }
        void Log(std::string entry) { m_log.push_back(std::move(entry)); }
        // 真の間は DecideStep で SkipStateStep を呼ぶ
        bool skip = false;

    protected:
        void DecideStep() override
        {
            Log("decide");
            if (skip)
            {
                SkipStateStep();
            }
        }
        void AfterStateStep() override { Log("after"); }
        void BodyStep() override { Log("body"); }
        void VisualStep() override { Log("visual"); }

    private:
        std::vector<std::string>& m_log;
    };

    void LoggingState::OnStep(StepProbeActor* owner, float)
    {
        NS_ASSERT(Game, owner != nullptr, "状態の持ち主が無い");
        owner->Log("state");
    }

    // 1 フレームの並びの管理役の段に、名前を控える管理役を 1 つずつ入れる
    struct PhaseTickers
    {
        explicit PhaseTickers(NS::Scene& scene, std::vector<std::string>& log)
            : m_scene(scene), physics(log, "physics"), sensors(log, "sensors"), course(log, "course"),
              camera(log, "camera"), ui(log, "ui"), effects(log, "effects")
        {
            m_scene.AddSystemTick(physics, NS::UpdatePhase::Physics);
            m_scene.AddSystemTick(sensors, NS::UpdatePhase::Sensors);
            m_scene.AddSystemTick(course, NS::UpdatePhase::Course);
            m_scene.AddSystemTick(camera, NS::UpdatePhase::Camera);
            m_scene.AddSystemTick(ui, NS::UpdatePhase::UI);
            m_scene.AddSystemTick(effects, NS::UpdatePhase::Effects);
        }
        ~PhaseTickers()
        {
            for (NS::SceneSystem* system :
                 std::initializer_list<NS::SceneSystem*>{&physics, &sensors, &course, &camera, &ui, &effects})
            {
                m_scene.RemoveSystemTick(*system);
            }
        }
        PhaseTickers(const PhaseTickers&) = delete;
        PhaseTickers& operator=(const PhaseTickers&) = delete;

    private:
        NS::Scene& m_scene;

    public:
        PhaseTicker physics;
        PhaseTicker sensors;
        PhaseTicker course;
        PhaseTicker camera;
        PhaseTicker ui;
        PhaseTicker effects;
    };
} // namespace

// アクターは段を選ばない。種類によらず全部が、物理の前の 1 か所で一覧の順に回る
TEST(ActorFrame, EveryActorRunsInTheListOrderBeforeThePhysics)
{
    NS::Scene scene;
    std::vector<std::string> log;
    scene.SpawnActor<FrameLogActor>("A", log, "a");
    scene.SpawnActor<FrameLogActor>("B", log, "b");
    const PhaseTickers tickers(scene, log);

    scene.OnUpdate();

    EXPECT_EQ(log,
              (std::vector<std::string>{"a",
                                        "b",
                                        "physics",
                                        "sensors",
                                        "course",
                                        "camera",
                                        "ui",
                                        "a:render-prep",
                                        "b:render-prep",
                                        "effects"}));
}

// 止めている間に Update するアクターは止め始めたアクターだけ。管理役は表のとおりで、描く支度は全部のアクター
TEST(OthersHold, OnlyTheHolderUpdatesAndTheSystemsFollowTheTable)
{
    NS::Scene scene;
    std::vector<std::string> log;
    FrameLogActor* other = scene.SpawnActor<FrameLogActor>("Other", log, "other");
    FrameLogActor* holder = scene.SpawnActor<FrameLogActor>("Holder", log, "holder");
    ASSERT_NE(other, nullptr);
    ASSERT_NE(holder, nullptr);
    const PhaseTickers tickers(scene, log);
    scene.HoldOthers(holder, 2);

    for (int i = 0; i < 3; ++i)
    {
        scene.OnUpdate();
    }

    EXPECT_FALSE(scene.IsHoldingOthers());
    for (const char* label : {"holder", "camera", "ui", "holder:render-prep", "other:render-prep"})
    {
        EXPECT_EQ(CountOf(log, label), 3) << label;
    }
    for (const char* label : {"other", "physics", "sensors", "course", "effects"})
    {
        EXPECT_EQ(CountOf(log, label), 1) << label;
    }
}

// Update の中で止め始めると、そのフレームの残りから効く。一覧で止め始めたアクターより後のアクターは回らない
TEST(OthersHold, AHoldStartedInAnUpdateSkipsTheActorsAfterTheHolderInThatFrame)
{
    NS::Scene scene;
    std::vector<std::string> log;
    scene.SpawnActor<FrameLogActor>("Before", log, "before");
    FrameLogActor* holder = scene.SpawnActor<FrameLogActor>("Holder", log, "holder");
    scene.SpawnActor<FrameLogActor>("After", log, "after");
    ASSERT_NE(holder, nullptr);
    holder->holdSteps = 1;

    scene.OnUpdate();
    EXPECT_EQ(CountOf(log, "before"), 1);
    EXPECT_EQ(CountOf(log, "holder"), 1);
    EXPECT_EQ(CountOf(log, "after"), 0);
    EXPECT_FALSE(scene.IsHoldingOthers());

    scene.OnUpdate();
    EXPECT_EQ(CountOf(log, "after"), 1);
}

// 止めは先に止め始めたアクターが持つ。止めている間は、ほかのアクターの頼みは延ばすのも解くのも受けない
TEST(OthersHold, TheFirstHolderKeepsTheHoldUntilItEndsOrReleasesIt)
{
    NS::Scene scene;
    std::vector<std::string> log;
    FrameLogActor* first = scene.SpawnActor<FrameLogActor>("First", log, "first");
    FrameLogActor* second = scene.SpawnActor<FrameLogActor>("Second", log, "second");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    scene.HoldOthers(first, 3);
    scene.HoldOthers(second, 10);
    scene.HoldOthers(second, 0);
    EXPECT_TRUE(scene.IsHoldingOthers());

    scene.OnUpdate();
    EXPECT_EQ(CountOf(log, "first"), 1);
    EXPECT_EQ(CountOf(log, "second"), 0);

    scene.HoldOthers(first, 0);
    EXPECT_FALSE(scene.IsHoldingOthers());
    scene.OnUpdate();
    EXPECT_EQ(CountOf(log, "first"), 2);
    EXPECT_EQ(CountOf(log, "second"), 1);
}

// 止め始めたアクターが消えても、消えたアクターの番地を読まない。止めは残りのフレームで切れ、後から入ったアクターも
// 止め始めたアクターと見なさない
TEST(OthersHold, RemovingTheHolderKeepsTheOthersHeldUntilTheHoldEnds)
{
    NS::Scene scene;
    std::vector<std::string> log;
    FrameLogActor* holder = scene.SpawnActor<FrameLogActor>("Holder", log, "holder");
    scene.SpawnActor<FrameLogActor>("Other", log, "other");
    ASSERT_NE(holder, nullptr);
    scene.HoldOthers(holder, 2);
    scene.RemoveActor(holder->Id());
    scene.SpawnActor<FrameLogActor>("Late", log, "late");

    scene.OnUpdate();
    scene.OnUpdate();
    EXPECT_EQ(CountOf(log, "other"), 0);
    EXPECT_EQ(CountOf(log, "late"), 0);
    EXPECT_FALSE(scene.IsHoldingOthers());

    scene.OnUpdate();
    EXPECT_EQ(CountOf(log, "other"), 1);
    EXPECT_EQ(CountOf(log, "late"), 1);
}

// 状態機械を進めるのは基盤だけ。進めたフレームは状態機械の後に AfterStateStep が 1 回呼ばれる
TEST(ActorStepOrder, UpdateRunsTheMachineAndThenAfterStateStepInTheFixedOrder)
{
    std::vector<std::string> log;
    StepProbeActor actor(log);
    actor.Build();
    actor.Update();
    EXPECT_EQ(log, (std::vector<std::string>{"decide", "state", "after", "body", "visual"}));
    log.clear();
    actor.Update();
    EXPECT_EQ(log, (std::vector<std::string>{"decide", "state", "after", "body", "visual"}));
}

// DecideStep の中で SkipStateStep を呼んだフレームだけ、状態機械も AfterStateStep も回らない
TEST(ActorStepOrder, SkipStateStepSkipsTheMachineAndAfterStateStepForThatFrameOnly)
{
    std::vector<std::string> log;
    StepProbeActor actor(log);
    actor.Build();
    actor.skip = true;
    const NS::uint32 stepsBefore = NS::GetStateStepCount(&actor);
    actor.Update();
    EXPECT_EQ(log, (std::vector<std::string>{"decide", "body", "visual"}));
    EXPECT_EQ(NS::GetStateStepCount(&actor), stepsBefore);

    actor.skip = false;
    log.clear();
    actor.Update();
    EXPECT_EQ(log, (std::vector<std::string>{"decide", "state", "after", "body", "visual"}));
    EXPECT_EQ(NS::GetStateStepCount(&actor), 1u);
}

// 止め (ヒットストップ) の間は状態機械を進めないので、AfterStateStep も呼ばない
TEST(ActorStepOrder, AfterStateStepRunsOnlyOnFramesThatAdvancedTheMachine)
{
    std::vector<std::string> log;
    StepProbeActor actor(log);
    actor.Build();
    actor.RequestHitStop(1);
    actor.Update();
    EXPECT_EQ(log, (std::vector<std::string>{"decide", "visual"}));
    log.clear();
    actor.Update();
    EXPECT_EQ(log, (std::vector<std::string>{"decide", "state", "after", "body", "visual"}));
}

TEST(UpdatePhase, SystemAddedDuringItsPhaseWaitsForTheNextFrame)
{
    // 段で呼ぶ物は段の始まりに決める。段の SceneActor が足した管理役をその段で呼ぶと、登録の時期で呼ぶ回数が変わる
    NS::Scene scene;
    CountingTicker late;
    scene.SpawnActor<LateRegisterActor>("LateRegisterActor", scene, late);

    scene.OnUpdate();
    EXPECT_EQ(late.ticks, 0);
    scene.OnUpdate();
    EXPECT_EQ(late.ticks, 1);
    scene.RemoveSystemTick(late);
}

TEST(UpdatePhase, DestroyingFromASystemTickIsRefused)
{
    // 管理役の更新も段の中。ここでアクターを解放すると、同じ段で後に呼ぶ管理役が破棄済みの相手を読む
    NS::Scene scene;
    CountingActor* victim = scene.SpawnActor<CountingActor>("Victim");
    const NS::uint32 victimId = victim->Id();
    DestroyerSystem destroyer(scene);
    destroyer.SetTarget(victimId);
    scene.AddSystemTick(destroyer, NS::UpdatePhase::Actors);

    const TestLogCapture log;
    scene.OnUpdate();
    EXPECT_EQ(log.ErrorCount(), 1);

    EXPECT_EQ(scene.Actors().FindByActorId(victimId), victim);
    scene.RemoveSystemTick(destroyer);
}

TEST(UpdatePhase, StableActorOrderAndRemovedTickerArePreserved)
{
    NS::Scene scene;
    std::vector<std::string> log;
    scene.SpawnActor<FrameLogActor>("First", log, "first");
    scene.SpawnActor<FrameLogActor>("Second", log, "second");
    PhaseTicker ticker(log, "ticker");
    scene.AddSystemTick(ticker, NS::UpdatePhase::Actors);
    scene.ExecutePhase(NS::UpdatePhase::Actors);
    // 登録物は段の SceneActor が出した物を受けてまとめる役なので、同じ段の SceneActor の後に動く
    EXPECT_EQ(log, (std::vector<std::string>{"first", "second", "ticker"}));
    scene.RemoveSystemTick(ticker);
    log.clear();
    scene.ExecutePhase(NS::UpdatePhase::Actors);
    EXPECT_EQ(log, (std::vector<std::string>{"first", "second"}));
}

TEST(UpdatePhase, DestroyingDuringAPhaseIsRefused)
{
    // 段の最中にアクターを解放すると、その段で後に呼ぶ予定の生ポインタが破棄済みになる。解放の口は段の間は断る
    NS::Scene scene;
    DestroyerActor* destroyer = scene.SpawnActor<DestroyerActor>("Destroyer", scene);
    CountingActor* victim = scene.SpawnActor<CountingActor>("Victim");
    const NS::uint32 victimId = victim->Id();
    destroyer->SetTarget(victimId);

    const TestLogCapture log;
    scene.ExecutePhase(NS::UpdatePhase::Actors);
    EXPECT_EQ(log.ErrorCount(), 1);

    ASSERT_EQ(scene.Actors().FindByActorId(victimId), victim);
    EXPECT_EQ(victim->updates, 1);
}

TEST(InterpolationSnapshot, ModelWrittenByAnEarlierPhaseStartsFromTheValueBeforeTheStep)
{
    // 前の値を書き手より後の段で控えると、先の段で書いた倍率が始点にもなり補間されずに飛ぶ
    NS::Scene scene;
    DrawModelActor* drawn = scene.SpawnActor<DrawModelActor>("DrawModelActor");
    ASSERT_NE(drawn->ModelClassObj(), nullptr);
    NS::Model* model = drawn->ModelClassObj();
    scene.SpawnActor<DrawScaleWriterActor>("DrawScaleWriterActor", drawn);

    scene.OnUpdate();

    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(drawn->Root().WorldMatrix(), 1.0f)._11, 2.0f);
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(drawn->Root().InterpolatedWorldMatrix(0.0f), 0.0f)._11, 1.0f);
}

TEST(InterpolationSnapshot, PausedSceneFreezesTheModelLikeTheRoot)
{
    // 止めている間は段が回らない。前の値の控えも段に置くと、止める前に書いた倍率から補間し続けて絵が揺れる
    NS::Scene scene;
    DrawModelActor* drawn = scene.SpawnActor<DrawModelActor>("DrawModelActor");
    ASSERT_NE(drawn->ModelClassObj(), nullptr);
    NS::Model* model = drawn->ModelClassObj();
    scene.SetSimulationPaused(true);
    ASSERT_TRUE(model->SetDrawScale(NS::Vector3{2.0f, 2.0f, 2.0f}));

    scene.OnUpdate();

    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(drawn->Root().InterpolatedWorldMatrix(0.0f), 0.0f)._11, 2.0f);
}

TEST(InterpolationSnapshot, SnapStartsTheModelAtTheNewScale)
{
    NS::Scene scene;
    DrawModelActor* drawn = scene.SpawnActor<DrawModelActor>("DrawModelActor");
    NS::Model* model = drawn->ModelClassObj();
    ASSERT_NE(model, nullptr);
    ASSERT_TRUE(model->SetDrawScale(NS::Vector3{2.0f, 2.0f, 2.0f}));
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(drawn->Root().InterpolatedWorldMatrix(0.0f), 0.0f)._11, 1.0f);

    ASSERT_TRUE(model->SnapDrawScale(NS::Vector3{2.0f, 2.0f, 2.0f}));

    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(drawn->Root().InterpolatedWorldMatrix(0.0f), 0.0f)._11, 2.0f);
}

TEST(WorldSpeed, SlowWorldStepsOnceInFiveAndRealTimePhasesEveryStep)
{
    // 遅い世界は 1 歩の中身を変えず、世界の時計の段を間引く。UI は毎歩回る
    NS::Scene scene;
    std::vector<std::string> log;
    scene.SpawnActor<FrameLogActor>("Actor", log, "actor");
    PhaseTicker physics(log, "physics");
    PhaseTicker ui(log, "ui");
    PhaseTicker effects(log, "effects");
    scene.AddSystemTick(physics, NS::UpdatePhase::Physics);
    scene.AddSystemTick(ui, NS::UpdatePhase::UI);
    scene.AddSystemTick(effects, NS::UpdatePhase::Effects);
    scene.SetWorldSpeed(0.2f);

    for (int i = 0; i < 10; ++i)
    {
        scene.OnUpdate();
    }

    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"actor"}), 2);
    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"physics"}), 2);
    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"effects"}), 2);
    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"actor:render-prep"}), 2);
    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"ui"}), 10);
    // 4 歩目までは UI だけ。5 歩目に初めて世界が進む
    ASSERT_GE(log.size(), 5u);
    EXPECT_EQ(log[3], std::string{"ui"});
    EXPECT_EQ(log[4], std::string{"actor"});
    scene.RemoveSystemTick(physics);
    scene.RemoveSystemTick(ui);
    scene.RemoveSystemTick(effects);
}

TEST(WorldSpeed, SlowWorldInterpolatesAcrossTheSkippedSteps)
{
    // 世界を進めない歩に前の値を控えると補間が止まる。割合は前に世界を進めてからの溜めで出す
    NS::Scene scene;
    DrawModelActor* drawn = scene.SpawnActor<DrawModelActor>("DrawModelActor");
    ASSERT_NE(drawn->ModelClassObj(), nullptr);
    NS::Model* model = drawn->ModelClassObj();
    scene.SpawnActor<DrawScaleWriterActor>("DrawScaleWriterActor", drawn);
    scene.SetWorldSpeed(0.5f);

    scene.OnUpdate();
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(drawn->Root().WorldMatrix(), 1.0f)._11, 1.0f);
    scene.OnUpdate();
    // 世界を進めた歩。前の値は進める前の倍率
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(drawn->Root().InterpolatedWorldMatrix(0.0f), 0.0f)._11, 1.0f);
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(drawn->Root().WorldMatrix(), 1.0f)._11, 2.0f);
    EXPECT_FLOAT_EQ(scene.RenderAlpha(0.5f), 0.25f);
    scene.OnUpdate();
    // 世界を進めない歩でも前の値を取り直さない
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(drawn->Root().InterpolatedWorldMatrix(0.0f), 0.0f)._11, 1.0f);
    EXPECT_FLOAT_EQ(scene.RenderAlpha(0.5f), 0.75f);
}

TEST(WorldSpeed, NormalSpeedKeepsTheFrameAlphaAndPauseHoldsAtOne)
{
    NS::Scene scene;
    EXPECT_FLOAT_EQ(scene.WorldSpeed(), 1.0f);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.RenderAlpha(0.3f), 0.3f);
    scene.SetWorldSpeed(2.0f);
    EXPECT_FLOAT_EQ(scene.WorldSpeed(), 1.0f);
    scene.SetWorldSpeed(-1.0f);
    EXPECT_FLOAT_EQ(scene.WorldSpeed(), 0.0f);
    scene.SetSimulationPaused(true);
    EXPECT_FLOAT_EQ(scene.RenderAlpha(0.3f), 1.0f);
    // プレイを入れ直すと普段の速さへ戻る
    scene.SetSimulationEnabled(true);
    EXPECT_FLOAT_EQ(scene.WorldSpeed(), 1.0f);
}

TEST(WorldSpeed, RampReturnsToNormalSpeedInRealSeconds)
{
    // 段階的な明けは実時間で戻す。世界の時間で数えると、遅い分だけ戻るのが延びる
    NS::Scene scene;
    const float dt = NS::OS::FrameTimer::FixedDelta();
    const int steps = static_cast<int>(std::ceil(0.3f / dt - 1.0e-3f));
    scene.StartWorldSpeedRamp(0.2f, 0.3f, NS::Curve{});
    EXPECT_FLOAT_EQ(scene.WorldSpeed(), 0.2f);
    float previous = scene.WorldSpeed();
    for (int i = 1; i < steps; ++i)
    {
        SCOPED_TRACE(i);
        scene.OnUpdate();
        EXPECT_GT(scene.WorldSpeed(), previous);
        EXPECT_LT(scene.WorldSpeed(), 1.0f);
        previous = scene.WorldSpeed();
    }
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.WorldSpeed(), 1.0f);
}

TEST(WorldSpeed, SettingTheSpeedStopsTheRamp)
{
    // 速さの持ち主は 1 つ。置き直した速さを戻りの曲線が上書きしない
    NS::Scene scene;
    scene.StartWorldSpeedRamp(0.2f, 0.3f, NS::Curve{});
    scene.SetWorldSpeed(0.5f);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.WorldSpeed(), 0.5f);
    scene.StartWorldSpeedRamp(0.2f, 0.3f, NS::Curve{});
    scene.SetSimulationEnabled(true);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.WorldSpeed(), 1.0f);
}

TEST(OthersHold, ZeroStepsAndRestartingThePlayReleaseTheHold)
{
    NS::Scene scene;
    const NS::SceneActor* holder = scene.SpawnActor<NS::SceneActor>("Holder");
    ASSERT_NE(holder, nullptr);
    scene.HoldOthers(holder, 3);
    scene.HoldOthers(holder, 0);
    EXPECT_FALSE(scene.IsHoldingOthers());
    scene.HoldOthers(holder, 3);
    scene.SetSimulationEnabled(true);
    EXPECT_FALSE(scene.IsHoldingOthers());
}

// 局所の倍率は形の中心で掛かり、根の位置を動かさない。描く倍率の中心 (下端の真ん中) は倍率を掛けた形から測る
TEST(ModelLocalScale, ScalesAroundTheMeshOriginAndMovesTheDrawScalePivot)
{
    NS::Scene scene;
    DrawModelActor* drawn = scene.SpawnActor<DrawModelActor>("DrawModelActor");
    NS::Model* model = drawn->ModelClassObj();
    ASSERT_NE(model, nullptr);
    model->SetLocalBoundsOverride(NS::AABB{NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{1.0f, 1.0f, 1.0f}});
    ASSERT_TRUE(model->SetLocalScale(NS::Vector3{2.0f, 2.0f, 2.0f}));
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(drawn->Root().WorldMatrix(), 1.0f)._11, 2.0f);
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(drawn->Root().WorldMatrix(), 1.0f)._42, 0.0f);

    // 倍率を掛けた形の下端は -2。縦に 2 倍すると、形の中心は下端から 2 × 2 の高さへ上がる
    ASSERT_TRUE(model->SnapDrawScale(NS::Vector3{1.0f, 2.0f, 1.0f}));
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(drawn->Root().WorldMatrix(), 1.0f)._42, 2.0f);

    EXPECT_FALSE(model->SetLocalScale(NS::Vector3{0.0f, 1.0f, 1.0f}));
    EXPECT_FLOAT_EQ(model->LocalScale().x, 2.0f);
}
