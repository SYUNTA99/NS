#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/Object/System/UIDirector.h"
#include "NSlib/Object/UIActor.h"
#include "NSlib/UI/ColorRect.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace
{
    // 進んだ順を控え、描く順を外から決める画面の物
    class OrderedUI final : public NS::Obj::UIActor
    {
    public:
        OrderedUI(std::vector<std::string>& log, std::string name, int drawOrder) noexcept
            : m_log(log), m_name(std::move(name)), m_drawOrder(drawOrder)
        {}
        void OnTick() override { m_log.push_back(m_name); }
        [[nodiscard]] int DrawOrder() const noexcept override { return m_drawOrder; }

    private:
        std::vector<std::string>& m_log;
        std::string m_name;
        int m_drawOrder = 0;
    };

    // 自分の番で、ほかの画面を開くか閉じる画面の物
    class TogglingUI final : public NS::Obj::UIActor
    {
    public:
        TogglingUI(NS::Obj::Scene& scene, NS::Obj::UIActor& target, bool opens) noexcept
            : m_scene(scene), m_target(target), m_opens(opens)
        {}
        void OnTick() override
        {
            if (m_opens)
            {
                m_target.Open(m_scene);
            }
            else
            {
                m_target.Close();
            }
        }

    private:
        NS::Obj::Scene& m_scene;
        NS::Obj::UIActor& m_target;
        bool m_opens = false;
    };
} // namespace

// 段で回る口は Actor だけが持ち、UIActor と ActorBase には無い
template <class T>
concept HasPhaseHooks = requires(T& actor) {
    actor.Phase();
    actor.ReadInput();
    actor.Update();
    actor.PrepareRender();
};
template <class T>
concept HasAnyPhaseHook = requires(T& actor) { actor.Phase(); } || requires(T& actor) { actor.ReadInput(); } ||
                          requires(T& actor) { actor.Update(); } || requires(T& actor) { actor.PrepareRender(); };
static_assert(HasPhaseHooks<NS::Obj::Actor>);
static_assert(!HasAnyPhaseHook<NS::Obj::UIActor>);
static_assert(!HasAnyPhaseHook<NS::Obj::ActorBase>);

// 更新の順は開いた順で、描く順とは別の一覧になる
TEST(UIActorWidgets, OpenScreensTickInOpenOrderOnTheUIPhase)
{
    NS::Obj::Scene scene;
    std::vector<std::string> log;
    OrderedUI front(log, "front", 1);
    OrderedUI back(log, "back", 0);
    front.Open(scene);
    back.Open(scene);

    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::UI);
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "front");
    EXPECT_EQ(log[1], "back");

    front.Close();
    log.clear();
    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::UI);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "back");
    back.Close();
}

// 描く順は DrawOrder の昇順で、同じ値は開いた順
TEST(UIActorWidgets, OpenScreensDrawInAscendingDrawOrder)
{
    NS::Obj::Scene scene;
    std::vector<std::string> log;
    OrderedUI front(log, "front", 1);
    OrderedUI back(log, "back", 0);
    OrderedUI frontLater(log, "frontLater", 1);
    front.Open(scene);
    back.Open(scene);
    frontLater.Open(scene);

    const std::vector<NS::Obj::UIActor*> order = scene.UI().DrawOrdered();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], &back);
    EXPECT_EQ(order[1], &front);
    EXPECT_EQ(order[2], &frontLater);
}

// 配置物を組み直しても、開いている画面は開いたまま動く
TEST(UIActorWidgets, OpenScreenKeepsTickingAfterTheWorldIsRebuilt)
{
    NS::Obj::Scene scene;
    std::vector<std::string> log;
    OrderedUI ui(log, "ui", 0);
    ui.Open(scene);

    scene.LoadJson(NS::Obj::MakeSceneJson());

    EXPECT_TRUE(ui.IsOpen());
    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::UI);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "ui");
}

// UI の段の最中に開いた画面は、その歩では動かない
TEST(UIActorWidgets, ScreenOpenedDuringTheUIPhaseWaitsForTheNextStep)
{
    NS::Obj::Scene scene;
    std::vector<std::string> log;
    OrderedUI late(log, "late", 0);
    TogglingUI opener(scene, late, true);
    opener.Open(scene);

    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::UI);
    EXPECT_TRUE(late.IsOpen());
    EXPECT_TRUE(log.empty());

    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::UI);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "late");
}

// UI の段の最中に閉じた画面は、その歩の残りで動かない
TEST(UIActorWidgets, ScreenClosedDuringTheUIPhaseIsSkipped)
{
    NS::Obj::Scene scene;
    std::vector<std::string> log;
    OrderedUI victim(log, "victim", 0);
    TogglingUI closer(scene, victim, false);
    closer.Open(scene);
    victim.Open(scene);

    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::UI);

    EXPECT_FALSE(victim.IsOpen());
    EXPECT_TRUE(log.empty());
}

TEST(UIActorWidgets, WidgetsStretchToTheViewport)
{
    NS::Obj::Scene scene;
    NS::Obj::UIActor ui;
    ui.Widgets().Root().AddChild<NS::UI::ColorRect>()->SetStretch(true);
    ui.Open(scene);
    ui.Widgets().Layout(1600.0f, 900.0f);
    EXPECT_NEAR(ui.Widgets().Root().LayoutRect().width, 1920.0f, 0.0001f);
    EXPECT_FLOAT_EQ(ui.Widgets().Root().LayoutRect().height, 1080.0f);
    ui.Close();
}
