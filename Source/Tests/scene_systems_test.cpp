#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/ITickable.h"
#include "Runtime/Object/IUse/IUseSceneObj.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/UIActor.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

// シーンに 1 つの物の置き場、画面に出す物 (UIActor)、部品でない物の更新の登録口を縛る

namespace
{
    class CountedSceneObj final : public NS::Obj::ISceneObj
    {
    public:
        explicit CountedSceneObj(NS::Obj::Scene& scene) noexcept
        {
            (void)scene;
            ++s_alive;
        }
        ~CountedSceneObj() noexcept override { --s_alive; }
        static inline int s_alive = 0;
    };

    class CountingUI final : public NS::Obj::UIActor
    {
    public:
        void OnUpdate() override { ++updates; }
        void OnRenderOverlay(const NS::Gfx::RenderContext&) override {}
        int updates = 0;
    };

    // 呼ばれた順を控える
    struct TickLog
    {
        std::vector<std::string> order;
    };

    class LoggingTicker final : public NS::Obj::ITickable
    {
    public:
        explicit LoggingTicker(TickLog& log) noexcept : m_log(log) {}
        void OnTick() override { m_log.order.push_back("ticker"); }

    private:
        TickLog& m_log;
    };

    class LoggingComponent final : public NS::Obj::Component
    {
    public:
        explicit LoggingComponent(TickLog* log = nullptr) noexcept : NS::Obj::Component(), m_log(log) {}
        void OnUpdate() override
        {
            if (m_log != nullptr)
            {
                m_log->order.push_back("component");
            }
        }
        NS_REFLECT_NONE(LoggingComponent, NS::Obj::Component)

    private:
        TickLog* m_log = nullptr;
    };

    class LoggingActor final : public NS::Obj::Actor
    {
    public:
        explicit LoggingActor(TickLog& log) noexcept : m_log(log), m_part(&log) { AttachFixedComponent(m_part); }

        void ForEachPart(const PartVisitor& visitor) const override
        {
            NS::Obj::Actor::ForEachPart(visitor);
            visitor("Logging", m_part);
        }

    protected:
        void StateStep() override
        {
            m_log.order.push_back("actor");
            TickPart(&m_part);
        }

    private:
        TickLog& m_log;
        mutable LoggingComponent m_part;
    };
} // namespace

TEST(SceneObjHolder, OneInstancePerTypeAndClearDestroys)
{
    NS::Obj::Scene scene;
    CountedSceneObj* first = NS::Obj::GetOrCreateSceneObj<CountedSceneObj>(scene);
    CountedSceneObj* second = NS::Obj::GetOrCreateSceneObj<CountedSceneObj>(scene);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_EQ(NS::Obj::FindSceneObj<CountedSceneObj>(scene), first);
    EXPECT_EQ(CountedSceneObj::s_alive, 1);

    // 配置物を組み直すと最初の状態から作り直させる
    scene.LoadJson(NS::Obj::MakeSceneJson());
    EXPECT_EQ(CountedSceneObj::s_alive, 0);
    EXPECT_EQ(NS::Obj::FindSceneObj<CountedSceneObj>(scene), nullptr);
}

TEST(SceneObjHolder, ActorReachesSceneObjectsThroughWindow)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* actor = scene.SpawnTransient<NS::Obj::Actor>();
    CountedSceneObj* made = NS::Obj::GetOrCreateSceneObj<CountedSceneObj>(*actor);
    EXPECT_EQ(made, NS::Obj::FindSceneObj<CountedSceneObj>(scene));
    NS::Obj::Actor loose;
    EXPECT_EQ(NS::Obj::GetOrCreateSceneObj<CountedSceneObj>(loose), nullptr);
}

TEST(UIActor, OpenRegistersForUpdateAndCloseRemoves)
{
    NS::Obj::Scene scene;
    CountingUI ui;
    ui.Open(scene);
    EXPECT_TRUE(ui.IsOpen());
    EXPECT_EQ(ui.OwningScene(), &scene);
    scene.OnUpdate();
    EXPECT_EQ(ui.updates, 1);

    ui.Close();
    EXPECT_FALSE(ui.IsOpen());
    scene.OnUpdate();
    EXPECT_EQ(ui.updates, 1);
}

TEST(ObjectListTicker, TickerRunsBeforeComponentsOfSameBand)
{
    NS::Obj::Scene scene;
    TickLog log;
    scene.SpawnTransient<LoggingActor>(log);
    LoggingTicker ticker(log);
    scene.Objects().AddTicker(&ticker, NS::Obj::UpdatePhase::Triggers);

    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Triggers);
    ASSERT_EQ(log.order.size(), 3u);
    EXPECT_EQ(log.order[0], "ticker");
    EXPECT_EQ(log.order[1], "actor");
    EXPECT_EQ(log.order[2], "component");

    scene.Objects().RemoveTicker(&ticker);
    log.order.clear();
    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Triggers);
    ASSERT_EQ(log.order.size(), 2u);
    EXPECT_EQ(log.order[0], "actor");
    EXPECT_EQ(log.order[1], "component");
}

TEST(ObjectListTicker, ActorTickFollowsBands)
{
    NS::Obj::Scene scene;
    TickLog log;
    scene.SpawnTransient<LoggingActor>(log);

    scene.OnUpdate();
    ASSERT_EQ(log.order.size(), 2u);
    EXPECT_EQ(log.order[0], "actor");
    EXPECT_EQ(log.order[1], "component");
}

// 数えるのは世界が実際に進んだ固定ステップだけ。止めている間に回った更新は数えない
TEST(SceneSimulation, StepCountAdvancesOnlyWhenTheWorldSteps)
{
    NS::Obj::Scene scene;
    const std::uint64_t start = scene.SimulationStepCount();
    scene.OnUpdate();
    EXPECT_EQ(scene.SimulationStepCount(), start + 1);

    scene.SetSimulationPaused(true);
    scene.OnUpdate();
    scene.OnUpdate();
    EXPECT_EQ(scene.SimulationStepCount(), start + 1);

    // コマ送りは 1 歩だけ進める
    scene.StepSimulation();
    scene.OnUpdate();
    scene.OnUpdate();
    EXPECT_EQ(scene.SimulationStepCount(), start + 2);

    scene.SetSimulationEnabled(false);
    scene.OnUpdate();
    EXPECT_EQ(scene.SimulationStepCount(), start + 2);
}
