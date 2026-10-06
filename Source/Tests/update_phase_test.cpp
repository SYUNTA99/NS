#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Components/Model.h"
#include "NSlib/Object/Reflection/Curve.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/UpdatePhase.h"
#include "NSlib/Windows/Clock.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
    class PhaseActor final : public NS::Obj::Actor
    {
    public:
        PhaseActor(NS::Obj::UpdatePhase phase, std::vector<std::string>& log, std::string label)
            : m_phase(phase), m_log(log), m_label(std::move(label))
        {}
        NS::Obj::UpdatePhase Phase() const noexcept override { return m_phase; }
        void ReadInput() override
        {
            if (m_phase == NS::Obj::UpdatePhase::Player)
            {
                m_log.push_back("input");
            }
        }
        void PrepareRender() override
        {
            if (m_phase == NS::Obj::UpdatePhase::Player)
            {
                m_log.push_back("render-prep");
            }
        }

    protected:
        void StateStep() override { m_log.push_back(m_label); }

    private:
        NS::Obj::UpdatePhase m_phase;
        std::vector<std::string>& m_log;
        std::string m_label;
    };

    // 1 フレームの 5 つの段をどれも上書きして、呼ばれた順を控える
    class StepOrderActor final : public NS::Obj::Actor
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

    // 自分の段の中で、控えた id の配置物をシーンから消しに行く
    class DestroyerActor final : public NS::Obj::Actor
    {
    public:
        explicit DestroyerActor(NS::Obj::Scene& scene) : m_scene(scene) {}
        NS::Obj::UpdatePhase Phase() const noexcept override { return NS::Obj::UpdatePhase::Triggers; }
        void SetTarget(std::uint32_t objectId) noexcept { m_target = objectId; }

    protected:
        void StateStep() override { m_scene.DestroyObject(m_target); }

    private:
        NS::Obj::Scene& m_scene;
        std::uint32_t m_target = NS::Obj::k_NoObjectId;
    };

    class CountingActor final : public NS::Obj::Actor
    {
    public:
        NS::Obj::UpdatePhase Phase() const noexcept override { return NS::Obj::UpdatePhase::Triggers; }
        int updates = 0;

    protected:
        void StateStep() override { ++updates; }
    };

    // Player の段で、別の Actor の Model の描く倍率を書く
    class DrawScaleWriterActor final : public NS::Obj::Actor
    {
    public:
        explicit DrawScaleWriterActor(NS::Obj::Model& target) : m_target(target) {}
        NS::Obj::UpdatePhase Phase() const noexcept override { return NS::Obj::UpdatePhase::Player; }

    protected:
        void StateStep() override { (void)m_target.SetDrawScale(NS::Vector3{2.0f, 2.0f, 2.0f}); }

    private:
        NS::Obj::Model& m_target;
    };

    class PhaseTicker final : public NS::Obj::ITickable
    {
    public:
        PhaseTicker(std::vector<std::string>& log, std::string label) : m_log(log), m_label(std::move(label)) {}
        void OnTick() override { m_log.push_back(m_label); }

    private:
        std::vector<std::string>& m_log;
        std::string m_label;
    };
} // namespace

TEST(UpdatePhase, SceneRunsTheNamedTableAroundPhysics)
{
    NS::Obj::Scene scene;
    std::vector<std::string> log;
    scene.SpawnTransient<PhaseActor>(NS::Obj::UpdatePhase::Camera, log, "camera");
    scene.SpawnTransient<PhaseActor>(NS::Obj::UpdatePhase::Triggers, log, "triggers");
    scene.SpawnTransient<PhaseActor>(NS::Obj::UpdatePhase::Enemy, log, "enemy");
    scene.SpawnTransient<PhaseActor>(NS::Obj::UpdatePhase::Player, log, "player");
    PhaseTicker sensors(log, "sensors");
    PhaseTicker course(log, "course");
    PhaseTicker ui(log, "ui");
    PhaseTicker effects(log, "effects");
    PhaseTicker physics(log, "physics");
    scene.Objects().AddTicker(&physics, NS::Obj::UpdatePhase::Physics);
    scene.Objects().AddTicker(&sensors, NS::Obj::UpdatePhase::Sensors);
    scene.Objects().AddTicker(&course, NS::Obj::UpdatePhase::Course);
    scene.Objects().AddTicker(&ui, NS::Obj::UpdatePhase::UI);
    scene.Objects().AddTicker(&effects, NS::Obj::UpdatePhase::Effects);
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
    scene.Objects().RemoveTicker(&physics);
    scene.Objects().RemoveTicker(&sensors);
    scene.Objects().RemoveTicker(&course);
    scene.Objects().RemoveTicker(&ui);
    scene.Objects().RemoveTicker(&effects);
}

TEST(UpdatePhase, StableActorOrderAndRemovedTickerArePreserved)
{
    NS::Obj::Scene scene;
    std::vector<std::string> log;
    scene.SpawnTransient<PhaseActor>(NS::Obj::UpdatePhase::Triggers, log, "first");
    scene.SpawnTransient<PhaseActor>(NS::Obj::UpdatePhase::Triggers, log, "second");
    PhaseTicker ticker(log, "ticker");
    scene.Objects().AddTicker(&ticker, NS::Obj::UpdatePhase::Triggers);
    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Triggers);
    // 登録物は段の Actor が出した物を受けてまとめる役なので、同じ段の Actor の後に動く
    EXPECT_EQ(log, (std::vector<std::string>{"first", "second", "ticker"}));
    scene.Objects().RemoveTicker(&ticker);
    log.clear();
    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Triggers);
    EXPECT_EQ(log, (std::vector<std::string>{"first", "second"}));
}

TEST(UpdatePhase, DestroyingDuringAPhaseIsRefused)
{
    // 段の最中に配置物を解放すると、その段で後に呼ぶ予定の生ポインタが破棄済みになる。解放の口は段の間は断る
    NS::Obj::Scene scene;
    std::unique_ptr<DestroyerActor> destroyerOwned = std::make_unique<DestroyerActor>(scene);
    std::unique_ptr<CountingActor> victimOwned = std::make_unique<CountingActor>();
    DestroyerActor* destroyer = destroyerOwned.get();
    CountingActor* victim = victimOwned.get();
    ASSERT_NE(scene.SpawnObject(std::move(destroyerOwned), "Destroyer"), nullptr);
    ASSERT_NE(scene.SpawnObject(std::move(victimOwned), "Victim"), nullptr);
    const std::uint32_t victimId = victim->Id();
    destroyer->SetTarget(victimId);

    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Triggers);

    ASSERT_EQ(scene.Objects().FindByObjectId(victimId), victim);
    EXPECT_EQ(victim->updates, 1);
}

TEST(InterpolationSnapshot, ModelWrittenByAnEarlierPhaseStartsFromTheValueBeforeTheStep)
{
    // 前の値を書き手より後の段で控えると、先の段で書いた倍率が始点にもなり補間されずに飛ぶ
    NS::Obj::Scene scene;
    NS::Obj::Actor* drawn = scene.SpawnTransient<NS::Obj::Actor>();
    ASSERT_NE(drawn->CreatePart("Model"), nullptr);
    NS::Obj::Model* model = drawn->ModelPart();
    scene.SpawnTransient<DrawScaleWriterActor>(*model);

    scene.OnUpdate();

    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(1.0f)._11, 2.0f);
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(0.0f)._11, 1.0f);
}

TEST(InterpolationSnapshot, PausedSceneFreezesTheModelLikeTheRoot)
{
    // 止めている間は段が回らない。前の値の控えも段に置くと、止める前に書いた倍率から補間し続けて絵が揺れる
    NS::Obj::Scene scene;
    NS::Obj::Actor* drawn = scene.SpawnTransient<NS::Obj::Actor>();
    ASSERT_NE(drawn->CreatePart("Model"), nullptr);
    NS::Obj::Model* model = drawn->ModelPart();
    scene.SetSimulationPaused(true);
    ASSERT_TRUE(model->SetDrawScale(NS::Vector3{2.0f, 2.0f, 2.0f}));

    scene.OnUpdate();

    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(0.0f)._11, 2.0f);
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
    NS::Obj::Scene scene;
    std::vector<std::string> log;
    scene.SpawnTransient<PhaseActor>(NS::Obj::UpdatePhase::Player, log, "player");
    PhaseTicker physics(log, "physics");
    PhaseTicker ui(log, "ui");
    PhaseTicker effects(log, "effects");
    scene.Objects().AddTicker(&physics, NS::Obj::UpdatePhase::Physics);
    scene.Objects().AddTicker(&ui, NS::Obj::UpdatePhase::UI);
    scene.Objects().AddTicker(&effects, NS::Obj::UpdatePhase::Effects);
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
    scene.Objects().RemoveTicker(&physics);
    scene.Objects().RemoveTicker(&ui);
    scene.Objects().RemoveTicker(&effects);
}

TEST(WorldSpeed, SlowWorldInterpolatesAcrossTheSkippedSteps)
{
    // 世界を進めない歩に前の値を控えると補間が止まる。割合は前に世界を進めてからの溜めで出す
    NS::Obj::Scene scene;
    NS::Obj::Actor* drawn = scene.SpawnTransient<NS::Obj::Actor>();
    ASSERT_NE(drawn->CreatePart("Model"), nullptr);
    NS::Obj::Model* model = drawn->ModelPart();
    scene.SpawnTransient<DrawScaleWriterActor>(*model);
    scene.SetWorldSpeed(0.5f);

    scene.OnUpdate();
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(1.0f)._11, 1.0f);
    scene.OnUpdate();
    // 世界を進めた歩。前の値は進める前の倍率
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(0.0f)._11, 1.0f);
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(1.0f)._11, 2.0f);
    EXPECT_FLOAT_EQ(scene.RenderAlpha(0.5f), 0.25f);
    scene.OnUpdate();
    // 世界を進めない歩でも前の値を取り直さない
    EXPECT_FLOAT_EQ(model->DrawWorldMatrix(0.0f)._11, 1.0f);
    EXPECT_FLOAT_EQ(scene.RenderAlpha(0.5f), 0.75f);
}

TEST(WorldSpeed, NormalSpeedKeepsTheFrameAlphaAndPauseHoldsAtOne)
{
    NS::Obj::Scene scene;
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
    NS::Obj::Scene scene;
    const float dt = NS::OS::FrameTimer::FixedDelta();
    const int steps = static_cast<int>(std::ceil(0.3f / dt - 1.0e-3f));
    scene.StartWorldSpeedRamp(0.2f, 0.3f, NS::Obj::Curve{});
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
    NS::Obj::Scene scene;
    scene.StartWorldSpeedRamp(0.2f, 0.3f, NS::Obj::Curve{});
    scene.SetWorldSpeed(0.5f);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.WorldSpeed(), 0.5f);
    scene.StartWorldSpeedRamp(0.2f, 0.3f, NS::Obj::Curve{});
    scene.SetSimulationEnabled(true);
    scene.OnUpdate();
    EXPECT_FLOAT_EQ(scene.WorldSpeed(), 1.0f);
}

TEST(OthersHold, HoldingOthersRunsOnlyThePlayerSideForItsSteps)
{
    // 真ん中で触れる前の数フレーム、自機以外の世界を止める。自機・入力・カメラ・UI は回す
    NS::Obj::Scene scene;
    std::vector<std::string> log;
    scene.SpawnTransient<PhaseActor>(NS::Obj::UpdatePhase::Player, log, "player");
    scene.SpawnTransient<PhaseActor>(NS::Obj::UpdatePhase::Enemy, log, "enemy");
    scene.SpawnTransient<PhaseActor>(NS::Obj::UpdatePhase::Triggers, log, "triggers");
    scene.SpawnTransient<PhaseActor>(NS::Obj::UpdatePhase::Camera, log, "camera");
    PhaseTicker physics(log, "physics");
    PhaseTicker sensors(log, "sensors");
    PhaseTicker course(log, "course");
    PhaseTicker ui(log, "ui");
    PhaseTicker effects(log, "effects");
    scene.Objects().AddTicker(&physics, NS::Obj::UpdatePhase::Physics);
    scene.Objects().AddTicker(&sensors, NS::Obj::UpdatePhase::Sensors);
    scene.Objects().AddTicker(&course, NS::Obj::UpdatePhase::Course);
    scene.Objects().AddTicker(&ui, NS::Obj::UpdatePhase::UI);
    scene.Objects().AddTicker(&effects, NS::Obj::UpdatePhase::Effects);
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
    scene.Objects().RemoveTicker(&physics);
    scene.Objects().RemoveTicker(&sensors);
    scene.Objects().RemoveTicker(&course);
    scene.Objects().RemoveTicker(&ui);
    scene.Objects().RemoveTicker(&effects);
}

TEST(OthersHold, ZeroStepsAndRestartingThePlayReleaseTheHold)
{
    NS::Obj::Scene scene;
    scene.HoldOthers(3);
    scene.HoldOthers(0);
    EXPECT_FALSE(scene.IsHoldingOthers());
    scene.HoldOthers(3);
    scene.SetSimulationEnabled(true);
    EXPECT_FALSE(scene.IsHoldingOthers());
}
