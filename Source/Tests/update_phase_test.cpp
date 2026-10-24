#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/UpdatePhase.h"
#include "NSlib/Windows/Clock.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace
{
    class PhaseActor final : public NS::SceneActor
    {
    public:
        PhaseActor(NS::UpdatePhase phase, std::vector<std::string>& log, std::string label)
            : m_phase(phase), m_log(log), m_label(std::move(label))
        {}
        NS::UpdatePhase Phase() const noexcept override { return m_phase; }
        void ReadInput() override
        {
            if (m_phase == NS::UpdatePhase::Player)
            {
                m_log.push_back("input");
            }
        }
        void PrepareRender() override
        {
            if (m_phase == NS::UpdatePhase::Player)
            {
                m_log.push_back("render-prep");
            }
        }

    protected:
        void StateStep() override { m_log.push_back(m_label); }

    private:
        NS::UpdatePhase m_phase;
        std::vector<std::string>& m_log;
        std::string m_label;
    };

    // 1 フレームの 5 つの段をどれも上書きして、呼ばれた順を控える
    class StepOrderActor final : public NS::SceneActor
    {
    public:
        explicit StepOrderActor(std::vector<std::string>& log) : m_log(log) {}

    protected:
        void ObserveStep() override { m_log.push_back("observe"); }
        void DecideStep() override { m_log.push_back("decide"); }
        void StateStep() override { m_log.push_back("state"); }
        void BodyStep() override { m_log.push_back("body"); }
        void VisualStep() override { m_log.push_back("visual"); }

    private:
        std::vector<std::string>& m_log;
    };

    // 自分の段の中で、控えた id のアクターをシーンから消しに行く
    class DestroyerActor final : public NS::SceneActor
    {
    public:
        explicit DestroyerActor(NS::Scene& scene) : m_scene(scene) {}
        NS::UpdatePhase Phase() const noexcept override { return NS::UpdatePhase::Triggers; }
        void SetTarget(NS::uint32 objectId) noexcept { m_target = objectId; }

    protected:
        void StateStep() override { m_scene.RemoveActor(m_target); }

    private:
        NS::Scene& m_scene;
        NS::uint32 m_target = NS::k_NoActorId;
    };

    class CountingActor final : public NS::SceneActor
    {
    public:
        NS::UpdatePhase Phase() const noexcept override { return NS::UpdatePhase::Triggers; }
        int updates = 0;

    protected:
        void StateStep() override { ++updates; }
    };

    class DrawModelActor final : public NS::SceneActor
    {
    protected:
        void OnInit() override { CreateClassObj(m_model); }
    };

    // 描く側より先に回る自機の段で書く。補間の始点が書く前の値で控えられたかを見る
    class DrawScaleWriterActor final : public NS::SceneActor
    {
    public:
        explicit DrawScaleWriterActor(DrawModelActor* target) : m_target(target)
        {
            NS_ASSERT(Game, target != nullptr, "DrawScaleWriterActor: 書く先のアクターが無い");
        }
        NS::UpdatePhase Phase() const noexcept override { return NS::UpdatePhase::Player; }

    protected:
        void StateStep() override { (void)m_target->ModelClassObj()->SetDrawScale(NS::Vector3{2.0f, 2.0f, 2.0f}); }

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

    // 自分の段の中で、渡された管理役を同じ段へ登録する
    class LateRegisterActor final : public NS::SceneActor
    {
    public:
        LateRegisterActor(NS::Scene& scene, NS::SceneSystem& system) : m_scene(scene), m_system(system) {}
        NS::UpdatePhase Phase() const noexcept override { return NS::UpdatePhase::Triggers; }

    protected:
        void StateStep() override { m_scene.AddSystemTick(m_system, NS::UpdatePhase::Triggers); }

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
} // namespace

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
    scene.AddSystemTick(destroyer, NS::UpdatePhase::Triggers);

    const TestLogCapture log;
    scene.OnUpdate();
    EXPECT_EQ(log.ErrorCount(), 1);

    EXPECT_EQ(scene.Actors().FindByActorId(victimId), victim);
    scene.RemoveSystemTick(destroyer);
}

TEST(UpdatePhase, SceneRunsTheNamedTableAroundPhysics)
{
    NS::Scene scene;
    std::vector<std::string> log;
    scene.SpawnActor<PhaseActor>("PhaseActor", NS::UpdatePhase::Camera, log, "camera");
    scene.SpawnActor<PhaseActor>("PhaseActor", NS::UpdatePhase::Triggers, log, "triggers");
    scene.SpawnActor<PhaseActor>("PhaseActor", NS::UpdatePhase::Enemy, log, "enemy");
    scene.SpawnActor<PhaseActor>("PhaseActor", NS::UpdatePhase::Player, log, "player");
    PhaseTicker sensors(log, "sensors");
    PhaseTicker course(log, "course");
    PhaseTicker ui(log, "ui");
    PhaseTicker effects(log, "effects");
    PhaseTicker physics(log, "physics");
    scene.AddSystemTick(physics, NS::UpdatePhase::Physics);
    scene.AddSystemTick(sensors, NS::UpdatePhase::Sensors);
    scene.AddSystemTick(course, NS::UpdatePhase::Course);
    scene.AddSystemTick(ui, NS::UpdatePhase::UI);
    scene.AddSystemTick(effects, NS::UpdatePhase::Effects);
    scene.OnUpdate();
    EXPECT_EQ(log,
              (std::vector<std::string>{"input",
                                        "player",
                                        "enemy",
                                        "physics",
                                        "sensors",
                                        "triggers",
                                        "course",
                                        "camera",
                                        "ui",
                                        "render-prep",
                                        "effects"}));
    scene.RemoveSystemTick(physics);
    scene.RemoveSystemTick(sensors);
    scene.RemoveSystemTick(course);
    scene.RemoveSystemTick(ui);
    scene.RemoveSystemTick(effects);
}

TEST(UpdatePhase, StableActorOrderAndRemovedTickerArePreserved)
{
    NS::Scene scene;
    std::vector<std::string> log;
    scene.SpawnActor<PhaseActor>("PhaseActor", NS::UpdatePhase::Triggers, log, "first");
    scene.SpawnActor<PhaseActor>("PhaseActor", NS::UpdatePhase::Triggers, log, "second");
    PhaseTicker ticker(log, "ticker");
    scene.AddSystemTick(ticker, NS::UpdatePhase::Triggers);
    scene.ExecutePhase(NS::UpdatePhase::Triggers);
    // 登録物は段の SceneActor が出した物を受けてまとめる役なので、同じ段の SceneActor の後に動く
    EXPECT_EQ(log, (std::vector<std::string>{"first", "second", "ticker"}));
    scene.RemoveSystemTick(ticker);
    log.clear();
    scene.ExecutePhase(NS::UpdatePhase::Triggers);
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
    scene.ExecutePhase(NS::UpdatePhase::Triggers);
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

TEST(ActorStepOrder, UpdateCallsEachStepOnceInTheFixedOrder)
{
    std::vector<std::string> log;
    StepOrderActor actor(log);
    actor.Update();
    EXPECT_EQ(log, (std::vector<std::string>{"observe", "decide", "state", "body", "visual"}));
    log.clear();
    actor.Update();
    EXPECT_EQ(log, (std::vector<std::string>{"observe", "decide", "state", "body", "visual"}));
}

TEST(WorldSpeed, SlowWorldStepsOnceInFiveAndRealTimePhasesEveryStep)
{
    // 遅い世界は 1 歩の中身を変えず、世界の時計の段を間引く。入力と UI は毎歩回る
    NS::Scene scene;
    std::vector<std::string> log;
    scene.SpawnActor<PhaseActor>("PhaseActor", NS::UpdatePhase::Player, log, "player");
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

    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"player"}), 2);
    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"physics"}), 2);
    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"effects"}), 2);
    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"render-prep"}), 2);
    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"ui"}), 10);
    EXPECT_EQ(std::count(log.begin(), log.end(), std::string{"input"}), 10);
    // 4 歩目までは入力と UI だけ。5 歩目に初めて世界が進む
    ASSERT_GE(log.size(), 10u);
    EXPECT_EQ(log[7], std::string{"ui"});
    EXPECT_EQ(log[8], std::string{"input"});
    EXPECT_EQ(log[9], std::string{"player"});
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

TEST(OthersHold, HoldingOthersRunsOnlyThePlayerSideForItsSteps)
{
    // 真ん中で触れる前の数フレーム、自機以外の世界を止める。自機・入力・カメラ・UI は回す
    NS::Scene scene;
    std::vector<std::string> log;
    scene.SpawnActor<PhaseActor>("PhaseActor", NS::UpdatePhase::Player, log, "player");
    scene.SpawnActor<PhaseActor>("PhaseActor", NS::UpdatePhase::Enemy, log, "enemy");
    scene.SpawnActor<PhaseActor>("PhaseActor", NS::UpdatePhase::Triggers, log, "triggers");
    scene.SpawnActor<PhaseActor>("PhaseActor", NS::UpdatePhase::Camera, log, "camera");
    PhaseTicker physics(log, "physics");
    PhaseTicker sensors(log, "sensors");
    PhaseTicker course(log, "course");
    PhaseTicker ui(log, "ui");
    PhaseTicker effects(log, "effects");
    scene.AddSystemTick(physics, NS::UpdatePhase::Physics);
    scene.AddSystemTick(sensors, NS::UpdatePhase::Sensors);
    scene.AddSystemTick(course, NS::UpdatePhase::Course);
    scene.AddSystemTick(ui, NS::UpdatePhase::UI);
    scene.AddSystemTick(effects, NS::UpdatePhase::Effects);
    scene.HoldOthers(2);
    EXPECT_TRUE(scene.IsHoldingOthers());

    for (int i = 0; i < 3; ++i)
    {
        scene.OnUpdate();
    }

    EXPECT_FALSE(scene.IsHoldingOthers());
    for (const char* label : {"input", "player", "camera", "ui", "render-prep"})
    {
        EXPECT_EQ(std::count(log.begin(), log.end(), std::string{label}), 3) << label;
    }
    for (const char* label : {"enemy", "physics", "sensors", "triggers", "course", "effects"})
    {
        EXPECT_EQ(std::count(log.begin(), log.end(), std::string{label}), 1) << label;
    }
    scene.RemoveSystemTick(physics);
    scene.RemoveSystemTick(sensors);
    scene.RemoveSystemTick(course);
    scene.RemoveSystemTick(ui);
    scene.RemoveSystemTick(effects);
}

TEST(OthersHold, ZeroStepsAndRestartingThePlayReleaseTheHold)
{
    NS::Scene scene;
    scene.HoldOthers(3);
    scene.HoldOthers(0);
    EXPECT_FALSE(scene.IsHoldingOthers());
    scene.HoldOthers(3);
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
