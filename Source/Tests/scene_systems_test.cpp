#include "Game/Level/CourseDirector.h"
#include "Game/Level/MapObj.h"
#include "Game/Player.h"
#include "NSlib/Graphics/DebugDraw.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/IUse/IUseSceneSystem.h"
#include "NSlib/Object/Message/Message.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/System/CameraManager.h"
#include "NSlib/Object/System/HitScreenDirector.h"
#include "NSlib/Object/System/HitSensorDirector.h"
#include "NSlib/Object/System/PadRumbleDirector.h"
#include "NSlib/Object/System/SceneRenderer.h"
#include "NSlib/Object/System/SceneSystem.h"
#include "NSlib/Object/System/UIDirector.h"
#include "NSlib/Object/UIActor.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

// シーンに 1 つの物の置き場、画面に出す物 (UIActor)、部品でない物の更新の登録口を縛る

// シーンの管理役は全部 SceneSystem の派生
static_assert(std::is_base_of_v<NS::Obj::SceneSystem, NS::Obj::SceneRenderer>);
static_assert(std::is_base_of_v<NS::Obj::SceneSystem, NS::Obj::HitSensorDirector>);
static_assert(std::is_base_of_v<NS::Obj::SceneSystem, NS::Obj::CameraManager>);
static_assert(std::is_base_of_v<NS::Obj::SceneSystem, NS::Obj::HitScreenDirector>);
static_assert(std::is_base_of_v<NS::Obj::SceneSystem, NS::Obj::PadRumbleDirector>);
static_assert(std::is_base_of_v<NS::Obj::SceneSystem, GL::Level::CourseDirector>);
static_assert(std::is_base_of_v<NS::Obj::SceneSystem, NS::Obj::UIDirector>);

// 更新の登録の口は Scene の 1 つで、SceneSystem の派生だけを受ける
template <class T>
concept CanAddSystemTick =
    requires(NS::Obj::Scene& scene, T& system) { scene.AddSystemTick(system, NS::Obj::UpdatePhase::Triggers); };
template <class T>
concept CanAddTickerDirectly =
    requires(NS::Obj::ActorList& list, T* system) { list.AddTicker(system, NS::Obj::UpdatePhase::Triggers); };
static_assert(CanAddSystemTick<NS::Obj::UIDirector>);
static_assert(!CanAddSystemTick<NS::Obj::Actor>);
static_assert(!CanAddSystemTick<NS::Obj::UIActor>);
static_assert(!CanAddTickerDirectly<NS::Obj::SceneSystem>);

// 知らせを受ける口は送る関数だけが呼べる
template <class T>
concept CanReceiveMsgDirectly =
    requires(T& actor, const NS::Obj::Message& msg) { actor.ReceiveMsg(msg, nullptr, nullptr); };
static_assert(!CanReceiveMsgDirectly<NS::Obj::Actor>);
static_assert(!CanReceiveMsgDirectly<Player>);
static_assert(!CanReceiveMsgDirectly<GL::Level::MapObj>);

namespace
{
    class CountedSceneObj final : public NS::Obj::SceneSystem
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

    // 作られた時に更新へ登録し、解除を自分では書かない管理役
    class SelfRegisteringSystem final : public NS::Obj::SceneSystem
    {
    public:
        explicit SelfRegisteringSystem(NS::Obj::Scene& scene)
        {
            scene.AddSystemTick(*this, NS::Obj::UpdatePhase::Triggers);
        }
        void OnTick() override { ++s_ticks; }
        static inline int s_ticks = 0;
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

    class LoggingTicker final : public NS::Obj::SceneSystem
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
        explicit LoggingActor(TickLog& log) noexcept : m_log(log) {}

    protected:
        void OnInit() override { m_part = CreateSubObj<LoggingSubObject>("Logging", &m_log); }
        void StateStep() override
        {
            m_log.order.push_back("actor");
            TickSubObj(m_part);
        }

    private:
        TickLog& m_log;
        LoggingSubObject* m_part = nullptr;
    };
} // namespace

TEST(SceneSystemHolder, OneInstancePerTypeAndClearDestroys)
{
    NS::Obj::Scene scene;
    CountedSceneObj* first = NS::Obj::GetOrCreateSceneSystem<CountedSceneObj>(&scene);
    CountedSceneObj* second = NS::Obj::GetOrCreateSceneSystem<CountedSceneObj>(&scene);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_EQ(NS::Obj::FindSceneSystem<CountedSceneObj>(&scene), first);
    EXPECT_EQ(CountedSceneObj::s_alive, 1);

    // 配置物を組み直すと最初の状態から作り直させる
    scene.LoadJson(NS::Obj::MakeSceneJson());
    EXPECT_EQ(CountedSceneObj::s_alive, 0);
    EXPECT_EQ(NS::Obj::FindSceneSystem<CountedSceneObj>(&scene), nullptr);
}

// 保持体は捨てる前に更新から外す。管理役はデストラクタに解除を書かなくてよい
TEST(SceneSystemHolder, ClearRemovesTheSystemFromTheUpdate)
{
    NS::Obj::Scene scene;
    SelfRegisteringSystem::s_ticks = 0;
    ASSERT_NE(NS::Obj::GetOrCreateSceneSystem<SelfRegisteringSystem>(&scene), nullptr);
    scene.OnUpdate();
    EXPECT_EQ(SelfRegisteringSystem::s_ticks, 1);

    scene.LoadJson(NS::Obj::MakeSceneJson());
    scene.OnUpdate();
    EXPECT_EQ(SelfRegisteringSystem::s_ticks, 1);
}

TEST(SceneSystemHolder, ActorReachesSceneObjectsThroughWindow)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* actor = scene.SpawnTransient<NS::Obj::Actor>();
    CountedSceneObj* made = NS::Obj::GetOrCreateSceneSystem<CountedSceneObj>(actor);
    EXPECT_EQ(made, NS::Obj::FindSceneSystem<CountedSceneObj>(&scene));
    NS::Obj::Actor loose;
    EXPECT_EQ(NS::Obj::GetOrCreateSceneSystem<CountedSceneObj>(&loose), nullptr);
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
    scene.AddSystemTick(ticker, NS::Obj::UpdatePhase::Triggers);

    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Triggers);
    ASSERT_EQ(log.order.size(), 3u);
    EXPECT_EQ(log.order[0], "actor");
    EXPECT_EQ(log.order[1], "component");
    EXPECT_EQ(log.order[2], "ticker");

    scene.RemoveSystemTick(ticker);
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
    DD::Line(NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{1.0f, 0.0f, 0.0f}, NS::Color{1.0f, 1.0f, 1.0f, 1.0f});
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
    DD::Line(NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{1.0f, 0.0f, 0.0f}, NS::Color{1.0f, 1.0f, 1.0f, 1.0f});

    scene.OnShutdown();

    EXPECT_EQ(DD::VertexCount(), std::size_t{0});
    DD::Clear();
}
