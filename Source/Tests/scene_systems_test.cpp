#include "NSlib/Graphics/DebugDraw.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/ITickable.h"
#include "NSlib/Object/IUse/IUseSceneObj.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/Object/UIActor.h"

#include <gtest/gtest.h>

#include <cstddef>
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
        void OnTick() override { ++updates; }
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

    class LoggingSubObject final : public NS::Obj::SubObject
    {
    public:
        explicit LoggingSubObject(TickLog* log = nullptr) noexcept : NS::Obj::SubObject(), m_log(log) {}
        void OnUpdate() override
        {
            if (m_log != nullptr)
            {
                m_log->order.push_back("component");
            }
        }
        NS_REFLECT_NONE(LoggingSubObject, NS::Obj::SubObject)

    private:
        TickLog* m_log = nullptr;
    };

    class LoggingActor final : public NS::Obj::Actor
    {
    public:
        explicit LoggingActor(TickLog& log) noexcept : m_log(log), m_part(&log) { AttachFixedSubObject(m_part); }

        void ForEachSubObj(const SubObjVisitor& visitor) const override
        {
            NS::Obj::Actor::ForEachSubObj(visitor);
            visitor("Logging", m_part);
        }

    protected:
        void StateStep() override
        {
            m_log.order.push_back("actor");
            TickSubObj(&m_part);
        }

    private:
        TickLog& m_log;
        mutable LoggingSubObject m_part;
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

TEST(ActorListTicker, TickerRunsAfterSubObjectsOfSameBand)
{
    NS::Obj::Scene scene;
    TickLog log;
    scene.SpawnTransient<LoggingActor>(log);
    LoggingTicker ticker(log);
    scene.Objects().AddTicker(&ticker, NS::Obj::UpdatePhase::Triggers);

    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Triggers);
    ASSERT_EQ(log.order.size(), 3u);
    EXPECT_EQ(log.order[0], "actor");
    EXPECT_EQ(log.order[1], "component");
    EXPECT_EQ(log.order[2], "ticker");

    scene.Objects().RemoveTicker(&ticker);
    log.order.clear();
    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Triggers);
    ASSERT_EQ(log.order.size(), 2u);
    EXPECT_EQ(log.order[0], "actor");
    EXPECT_EQ(log.order[1], "component");
}

TEST(ActorListTicker, ActorTickFollowsBands)
{
    NS::Obj::Scene scene;
    TickLog log;
    scene.SpawnTransient<LoggingActor>(log);

    scene.OnUpdate();
    ASSERT_EQ(log.order.size(), 2u);
    EXPECT_EQ(log.order[0], "actor");
    EXPECT_EQ(log.order[1], "component");
}

// ステップの図形は世界の物なので、世界を組み直したら前の世界の線と面を残さない
TEST(SceneStepShapes, RebuildingTheWorldDropsTheStepShapes)
{
    namespace DD = NS::Gfx::DebugDraw;
    NS::Obj::Scene scene;
    DD::Clear();
    DD::Line(NS::Vector3{0.0f, 0.0f, 0.0f},
             NS::Vector3{1.0f, 0.0f, 0.0f},
             NS::Color{1.0f, 1.0f, 1.0f, 1.0f});
    DD::Triangle(NS::Vector3{0.0f, 0.0f, 0.0f},
                 NS::Vector3{1.0f, 0.0f, 0.0f},
                 NS::Vector3{0.0f, 1.0f, 0.0f},
                 NS::Color{1.0f, 1.0f, 1.0f, 0.5f});

    scene.LoadJson(NS::Obj::MakeSceneJson());

    EXPECT_EQ(DD::VertexCount(), std::size_t{0});
    EXPECT_EQ(DD::FaceVertexCount(), std::size_t{0});
    DD::Clear();
}

// シーンを畳んだ後に、畳んだ世界の線が次のシーンの最初の描画へ残らない
TEST(SceneStepShapes, ShutdownDropsTheStepShapes)
{
    namespace DD = NS::Gfx::DebugDraw;
    NS::Obj::Scene scene;
    DD::Clear();
    DD::Line(NS::Vector3{0.0f, 0.0f, 0.0f},
             NS::Vector3{1.0f, 0.0f, 0.0f},
             NS::Color{1.0f, 1.0f, 1.0f, 1.0f});

    scene.OnShutdown();

    EXPECT_EQ(DD::VertexCount(), std::size_t{0});
    DD::Clear();
}
