#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/UpdatePhase.h"

#include <gtest/gtest.h>

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
    EXPECT_EQ(log, (std::vector<std::string>{"ticker", "first", "second"}));
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
