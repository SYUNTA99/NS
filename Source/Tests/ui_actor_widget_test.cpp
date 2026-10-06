#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/ScreenFade.h"
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

TEST(UIActorWidgets, FadeOwnsAFullScreenWidgetAndKeepsTheExistingAlphaTiming)
{
    NS::Obj::Scene scene;
    NS::Obj::ScreenFade fade;
    EXPECT_TRUE(fade.Widgets().Root().HasChildren());
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), 0.0f);
    fade.Open(scene);
    fade.Widgets().Layout(1600.0f, 900.0f);
    EXPECT_NEAR(fade.Widgets().Root().LayoutRect().width, 1920.0f, 0.0001f);
    EXPECT_FLOAT_EQ(fade.Widgets().Root().LayoutRect().height, 1080.0f);
    fade.BeginOut(0.4f);
    fade.Advance(0.1f);
    EXPECT_FLOAT_EQ(fade.Alpha(), 0.25f);
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), fade.Alpha());
    fade.Advance(0.3f);
    EXPECT_TRUE(fade.IsBlack());
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), 1.0f);
    fade.BeginIn(0.4f);
    fade.Advance(0.1f);
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), 0.75f);
    fade.Cancel();
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), 0.0f);
    fade.Close();
}

TEST(UIActorWidgets, ZeroDurationFadeChangesTheWidgetImmediately)
{
    NS::Obj::ScreenFade fade;
    fade.BeginOut(0.0f);
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), 1.0f);
    fade.BeginIn(0.0f);
    EXPECT_FLOAT_EQ(fade.Widgets().Root().Alpha(), 0.0f);
}
