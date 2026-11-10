#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/StateMachineOf.h"
#include "NSlib/CoreObject/UIActor.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
#include <algorithm>
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

    // 動かされるたびに名前を控える画面
    class LoggingUI final : public NS::UIActor
    {
    public:
        LoggingUI(std::vector<std::string>& log, std::string label) : m_log(log), m_label(std::move(label)) {}
        void OnTick() override { m_log.push_back(m_label); }
        void OnDraw(const NS::Gfx::RenderContext&) override {}

    private:
        std::vector<std::string>& m_log;
        std::string m_label;
    };

    // ゲームの流れ (StepGame) の中で、控えた id のアクターをシーンから消しに行くシーン
    class DestroyerScene final : public NS::Scene
    {
    public:
        void SetTarget(NS::uint32 objectId) noexcept { m_target = objectId; }

    protected:
        void StepGame() override { RemoveActor(m_target); }

    private:
        NS::uint32 m_target = NS::k_NoActorId;
    };

    // 更新のたびに名前を、描く支度のたびに名前と ":render-prep" を控える
    class FrameLogActor final : public NS::SceneActor
    {
    public:
        FrameLogActor(std::vector<std::string>& log, std::string label) : m_log(log), m_label(std::move(label)) {}
        void PrepareRender() override { m_log.push_back(m_label + ":render-prep"); }

    protected:
        void DecideStep() override { m_log.push_back(m_label); }

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

    // ゲームの流れ (StepGame) で "course" を控えるシーン
    class CourseLogScene final : public NS::Scene
    {
    public:
        explicit CourseLogScene(std::vector<std::string>& log) : m_log(log) {}

    protected:
        void StepGame() override { m_log.push_back("course"); }

    private:
        std::vector<std::string>& m_log;
    };
} // namespace

// アクターは種類によらず全部が、歩の初めの 1 か所で一覧の順に回る。ゲームの流れ・UI・描く支度はその後
TEST(ActorFrame, EveryActorRunsInTheListOrderBeforeTheGameFlowAndUI)
{
    std::vector<std::string> log;
    CourseLogScene scene(log);
    scene.SpawnActor<FrameLogActor>("A", log, "a");
    scene.SpawnActor<FrameLogActor>("B", log, "b");
    LoggingUI ui(log, "ui");
    ui.Open(scene);

    scene.OnUpdate();

    EXPECT_EQ(log, (std::vector<std::string>{"a", "b", "course", "ui", "a:render-prep", "b:render-prep"}));
    ui.Close();
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

TEST(SceneStepGuard, DestroyingFromTheGameFlowIsRefused)
{
    // ゲームの流れも歩の中。ここでアクターを解放すると、同じ歩で後に描く支度をするアクターが破棄済みになる
    DestroyerScene scene;
    CountingActor* victim = scene.SpawnActor<CountingActor>("Victim");
    const NS::uint32 victimId = victim->Id();
    scene.SetTarget(victimId);

    const TestLogCapture log;
    scene.OnUpdate();
    EXPECT_EQ(log.ErrorCount(), 1);

    EXPECT_EQ(scene.Actors().FindByActorId(victimId), victim);
}

TEST(SceneStepGuard, DestroyingDuringAStepIsRefused)
{
    // 歩の最中にアクターを解放すると、その歩で後に呼ぶ予定の生ポインタが破棄済みになる。解放の口は歩の間は断る
    NS::Scene scene;
    DestroyerActor* destroyer = scene.SpawnActor<DestroyerActor>("Destroyer", scene);
    CountingActor* victim = scene.SpawnActor<CountingActor>("Victim");
    const NS::uint32 victimId = victim->Id();
    destroyer->SetTarget(victimId);

    const TestLogCapture log;
    scene.OnUpdate();
    EXPECT_EQ(log.ErrorCount(), 1);

    ASSERT_EQ(scene.Actors().FindByActorId(victimId), victim);
    EXPECT_EQ(victim->updates, 1);
}

TEST(InterpolationSnapshot, ModelWrittenDuringTheStepStartsFromTheValueBeforeTheStep)
{
    // 前の値をアクターを回した後に控えると、その歩で書いた倍率が始点にもなり補間されずに飛ぶ
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
    // 止めている間はアクターが回らない。前の値の控えもアクターと一緒に回すと、止める前に書いた倍率から補間し続けて絵が揺れる
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

// 止まっている歩はアクターの Update と UI を回し、ゲームの流れ StepGame を回さない
TEST(ActorFrame, AStoppedStepSkipsTheGameFlowButKeepsActorsAndUI)
{
    std::vector<std::string> log;
    CourseLogScene scene(log);
    FrameLogActor* actor = scene.SpawnActor<FrameLogActor>("A", log, "a");
    LoggingUI ui(log, "ui");
    ui.Open(scene);
    scene.RequestStopScene(actor, 10);

    scene.OnUpdate();

    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"course"}), 0);
    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"a"}), 1);
    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"ui"}), 1);
    ui.Close();
}

TEST(RenderAlpha, KeepsTheFrameAlphaAndPauseHoldsAtOne)
{
    NS::Scene scene;
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.RenderAlpha(0.3f), 0.3f);
    scene.SetSimulationPaused(true);
    EXPECT_FLOAT_EQ(scene.RenderAlpha(0.3f), 1.0f);
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
