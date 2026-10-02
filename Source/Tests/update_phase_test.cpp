#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/UpdatePhase.h"

#include <gtest/gtest.h>

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
        void Update() override { m_log.push_back(m_label); }
        void PrepareRender() override
        {
            if (m_phase == NS::Obj::UpdatePhase::Player)
            {
                m_log.push_back("render-prep");
            }
        }

    private:
        NS::Obj::UpdatePhase m_phase;
        std::vector<std::string>& m_log;
        std::string m_label;
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
