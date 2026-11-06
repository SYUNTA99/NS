#include "Game/Level/FollowCameraActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Message/Message.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/UIActor.h"
#include "NSlib/Gfx/Render/DebugDraw.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

// 画面に出す物 (UIActor)、アクターを回す口、知らせを受ける口を縛る

// アクターを回す口は Scene::OnUpdate の 1 つ。アクターの一覧を直に回すと、歩を囲む BeginStep・EndStep と止めの効かせ方を通らない
template <class T>
concept CanUpdateActors = requires(T& target) { target.UpdateActors(); };
static_assert(!CanUpdateActors<NS::ActorList>);

// 知らせを受ける口は送る関数だけが呼べる
template <class T>
concept CanReceiveMsgDirectly = requires(T& actor, const NS::Message& msg) { actor.ReceiveMsg(msg, nullptr, nullptr); };
static_assert(!CanReceiveMsgDirectly<NS::SceneActor>);
static_assert(!CanReceiveMsgDirectly<PlayerActor>);
static_assert(!CanReceiveMsgDirectly<GL::Level::FollowCameraActor>);

namespace
{
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
        void DecideStep() override
        {
            m_log.order.push_back("actor");
            m_part->Log();
        }

    private:
        TickLog& m_log;
        std::unique_ptr<LoggingClassObj> m_part;
    };
} // namespace

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

TEST(ActorListTicker, ActorRunsBeforeItsPartInItsOwnStep)
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
    NS::Scene scene;
    NS::Gfx::ClearDebugDraw();
    NS::Gfx::DrawDebugLine(
        NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{1.0f, 0.0f, 0.0f}, NS::Color{1.0f, 1.0f, 1.0f, 1.0f});
    NS::Gfx::DrawDebugTriangle(NS::Vector3{0.0f, 0.0f, 0.0f},
                               NS::Vector3{1.0f, 0.0f, 0.0f},
                               NS::Vector3{0.0f, 1.0f, 0.0f},
                               NS::Color{1.0f, 1.0f, 1.0f, 0.5f});

    scene.ResetFromJson(NS::MakeSceneJson());

    EXPECT_EQ(NS::Gfx::DebugDrawVertexCount(), std::size_t{0});
    EXPECT_EQ(NS::Gfx::DebugDrawFaceVertexCount(), std::size_t{0});
    NS::Gfx::ClearDebugDraw();
}

// シーンを畳んだ後に、畳んだ世界の線が次のシーンの最初の描画へ残らない
TEST(SceneStepShapes, ShutdownDropsTheStepShapes)
{
    NS::Scene scene;
    NS::Gfx::ClearDebugDraw();
    NS::Gfx::DrawDebugLine(
        NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{1.0f, 0.0f, 0.0f}, NS::Color{1.0f, 1.0f, 1.0f, 1.0f});

    scene.OnShutdown();

    EXPECT_EQ(NS::Gfx::DebugDrawVertexCount(), std::size_t{0});
    NS::Gfx::ClearDebugDraw();
}
