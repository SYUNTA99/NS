#include "Game/Level/CourseDirector.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/Message/Message.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/System/HitScreenDirector.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"
#include "NSlib/CoreObject/System/PadRumbleDirector.h"
#include "NSlib/CoreObject/System/PhysicsDirector.h"
#include "NSlib/CoreObject/System/SceneRenderer.h"
#include "NSlib/CoreObject/System/SceneSystem.h"
#include "NSlib/CoreObject/System/ScreenFadeDirector.h"
#include "NSlib/CoreObject/System/UIDirector.h"
#include "NSlib/CoreObject/UIActor.h"
#include "NSlib/Gfx/Render/DebugDraw.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

// シーンに 1 つの物の置き場、画面に出す物 (UIActor)、部品でない物の更新の登録口を縛る

// シーンの管理役は全部 SceneSystem の派生
static_assert(std::is_base_of_v<NS::SceneSystem, NS::SceneRenderer>);
static_assert(std::is_base_of_v<NS::SceneSystem, NS::HitSensorDirector>);
static_assert(std::is_base_of_v<NS::SceneSystem, NS::CameraManager>);
static_assert(std::is_base_of_v<NS::SceneSystem, NS::HitScreenDirector>);
static_assert(std::is_base_of_v<NS::SceneSystem, NS::PadRumbleDirector>);
static_assert(std::is_base_of_v<NS::SceneSystem, GL::Level::CourseDirector>);
static_assert(std::is_base_of_v<NS::SceneSystem, NS::UIDirector>);
static_assert(std::is_base_of_v<NS::SceneSystem, NS::PhysicsDirector>);
static_assert(std::is_base_of_v<NS::SceneSystem, NS::ScreenFadeDirector>);

// 更新の登録の口は Scene の 1 つで、SceneSystem の派生だけを受ける
template <class T>
concept CanAddSystemTick =
    requires(NS::Scene& scene, T& system) { scene.AddSystemTick(system, NS::UpdatePhase::Triggers); };
static_assert(CanAddSystemTick<NS::UIDirector>);
static_assert(!CanAddSystemTick<NS::SceneActor>);
static_assert(!CanAddSystemTick<NS::UIActor>);

// 段を回す口も Scene の 1 つ。配置物の一覧を直に回すと、同じ段の管理役が呼ばれない
template <class T>
concept CanExecutePhase = requires(T& target) { target.ExecutePhase(NS::UpdatePhase::Triggers); };
static_assert(CanExecutePhase<NS::Scene>);
static_assert(!CanExecutePhase<NS::ActorList>);

// 知らせを受ける口は送る関数だけが呼べる
template <class T>
concept CanReceiveMsgDirectly = requires(T& actor, const NS::Message& msg) { actor.ReceiveMsg(msg, nullptr, nullptr); };
static_assert(!CanReceiveMsgDirectly<NS::SceneActor>);
static_assert(!CanReceiveMsgDirectly<PlayerActor>);
static_assert(!CanReceiveMsgDirectly<GL::Level::PropActor>);

namespace
{
    class CountedSceneObj final : public NS::SceneSystem
    {
    public:
        explicit CountedSceneObj(NS::Scene& scene) noexcept
        {
            (void)scene;
            ++s_alive;
        }
        ~CountedSceneObj() noexcept override { --s_alive; }
        static inline int s_alive = 0;
    };

    // 作られた時に更新へ登録し、解除を自分では書かない管理役
    class SelfRegisteringSystem final : public NS::SceneSystem
    {
    public:
        explicit SelfRegisteringSystem(NS::Scene& scene) { scene.AddSystemTick(*this, NS::UpdatePhase::Triggers); }
        void OnTick() override { ++s_ticks; }
        static inline int s_ticks = 0;
    };

    class CountingUI final : public NS::UIActor
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

    class LoggingTicker final : public NS::SceneSystem
    {
    public:
        explicit LoggingTicker(TickLog& log) noexcept : m_log(log) {}
        void OnTick() override { m_log.order.push_back("ticker"); }

    private:
        TickLog& m_log;
    };

    class LoggingClassObj final : public NS::Object
    {
    public:
        explicit LoggingClassObj(TickLog* log = nullptr) noexcept : m_log(log) {}
        void Log()
        {
            if (m_log != nullptr)
            {
                m_log->order.push_back("component");
            }
        }
        NS_REFLECT_NONE(LoggingClassObj, NS::Object)

    private:
        TickLog* m_log = nullptr;
    };

    class LoggingActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(LoggingActor, NS::SceneActor)
        NS_CLASSOBJ(m_part, "Logging")
        NS_REFLECT_END()

        explicit LoggingActor(TickLog& log) noexcept : m_log(log) {}

    protected:
        void OnInit() override { CreateClassObj(m_part, &m_log); }
        void StateStep() override
        {
            m_log.order.push_back("actor");
            m_part->Log();
        }

    private:
        TickLog& m_log;
        std::unique_ptr<LoggingClassObj> m_part;
    };
} // namespace

TEST(SceneSystemHolder, OneInstancePerTypeAndClearDestroys)
{
    NS::Scene scene;
    CountedSceneObj* first = NS::GetOrCreateSceneSystem<CountedSceneObj>(&scene);
    CountedSceneObj* second = NS::GetOrCreateSceneSystem<CountedSceneObj>(&scene);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_EQ(NS::FindSceneSystem<CountedSceneObj>(&scene), first);
    EXPECT_EQ(CountedSceneObj::s_alive, 1);

    // 配置物を組み直すと最初の状態から作り直させる
    scene.ResetFromJson(NS::MakeSceneJson());
    EXPECT_EQ(CountedSceneObj::s_alive, 0);
    EXPECT_EQ(NS::FindSceneSystem<CountedSceneObj>(&scene), nullptr);
}

// 保持体は捨てる前に更新から外す。管理役はデストラクタに解除を書かなくてよい
TEST(SceneSystemHolder, ClearRemovesTheSystemFromTheUpdate)
{
    NS::Scene scene;
    SelfRegisteringSystem::s_ticks = 0;
    ASSERT_NE(NS::GetOrCreateSceneSystem<SelfRegisteringSystem>(&scene), nullptr);
    scene.OnUpdate();
    EXPECT_EQ(SelfRegisteringSystem::s_ticks, 1);

    scene.ResetFromJson(NS::MakeSceneJson());
    scene.OnUpdate();
    EXPECT_EQ(SelfRegisteringSystem::s_ticks, 1);
}

TEST(SceneSystemHolder, ActorReachesSceneObjectsThroughWindow)
{
    NS::Scene scene;
    NS::SceneActor* actor = scene.SpawnActor<NS::SceneActor>("SceneActor");
    CountedSceneObj* made = NS::GetOrCreateSceneSystem<CountedSceneObj>(actor);
    EXPECT_EQ(made, NS::FindSceneSystem<CountedSceneObj>(&scene));
    NS::SceneActor loose;
    EXPECT_EQ(NS::GetOrCreateSceneSystem<CountedSceneObj>(&loose), nullptr);
}

TEST(UIActor, OpenRegistersForUpdateAndCloseRemoves)
{
    NS::Scene scene;
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

TEST(ActorListTicker, TickerRunsAfterClassObjsOfSameBand)
{
    NS::Scene scene;
    TickLog log;
    scene.SpawnActor<LoggingActor>("LoggingActor", log);
    LoggingTicker ticker(log);
    scene.AddSystemTick(ticker, NS::UpdatePhase::Triggers);

    scene.ExecutePhase(NS::UpdatePhase::Triggers);
    ASSERT_EQ(log.order.size(), 3u);
    EXPECT_EQ(log.order[0], "actor");
    EXPECT_EQ(log.order[1], "component");
    EXPECT_EQ(log.order[2], "ticker");

    scene.RemoveSystemTick(ticker);
    log.order.clear();
    scene.ExecutePhase(NS::UpdatePhase::Triggers);
    ASSERT_EQ(log.order.size(), 2u);
    EXPECT_EQ(log.order[0], "actor");
    EXPECT_EQ(log.order[1], "component");
}

TEST(ActorListTicker, ActorTickFollowsBands)
{
    NS::Scene scene;
    TickLog log;
    scene.SpawnActor<LoggingActor>("LoggingActor", log);

    scene.OnUpdate();
    ASSERT_EQ(log.order.size(), 2u);
    EXPECT_EQ(log.order[0], "actor");
    EXPECT_EQ(log.order[1], "component");
}

// ステップの図形は世界の物なので、世界を組み直したら前の世界の線と面を残さない
TEST(SceneStepShapes, RebuildingTheWorldDropsTheStepShapes)
{
    namespace DD = NS::Gfx::DebugDraw;
    NS::Scene scene;
    DD::Clear();
    DD::Line(NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{1.0f, 0.0f, 0.0f}, NS::Color{1.0f, 1.0f, 1.0f, 1.0f});
    DD::Triangle(NS::Vector3{0.0f, 0.0f, 0.0f},
                 NS::Vector3{1.0f, 0.0f, 0.0f},
                 NS::Vector3{0.0f, 1.0f, 0.0f},
                 NS::Color{1.0f, 1.0f, 1.0f, 0.5f});

    scene.ResetFromJson(NS::MakeSceneJson());

    EXPECT_EQ(DD::VertexCount(), std::size_t{0});
    EXPECT_EQ(DD::FaceVertexCount(), std::size_t{0});
    DD::Clear();
}

// シーンを畳んだ後に、畳んだ世界の線が次のシーンの最初の描画へ残らない
TEST(SceneStepShapes, ShutdownDropsTheStepShapes)
{
    namespace DD = NS::Gfx::DebugDraw;
    NS::Scene scene;
    DD::Clear();
    DD::Line(NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{1.0f, 0.0f, 0.0f}, NS::Color{1.0f, 1.0f, 1.0f, 1.0f});

    scene.OnShutdown();

    EXPECT_EQ(DD::VertexCount(), std::size_t{0});
    DD::Clear();
}
