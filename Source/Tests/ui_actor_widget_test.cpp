#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/System/UIDirector.h"
#include "NSlib/CoreObject/UIActor.h"
#include "NSlib/Gfx/Render/BuiltinShaders.h"
#include "NSlib/Gfx/Render/ScreenOverlay.h"
#include "NSlib/Gfx/Render/RenderContext.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/UI/ColorRect.h"
#include "NSlib/UI/UICanvas.h"
#include "NSlib/Core/Windows/Window.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace
{
    // 進んだ順を控え、描く順を外から決める画面の物
    class OrderedUI final : public NS::UIActor
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
    class TogglingUI final : public NS::UIActor
    {
    public:
        TogglingUI(NS::Scene& scene, NS::UIActor& target, bool opens) noexcept
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
        NS::Scene& m_scene;
        NS::UIActor& m_target;
        bool m_opens = false;
    };
} // namespace

// 段で回る口は SceneActor だけが持ち、UIActor には無い
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
static_assert(HasPhaseHooks<NS::SceneActor>);
static_assert(!HasAnyPhaseHook<NS::UIActor>);

// 更新の順は開いた順で、描く順とは別の一覧になる
TEST(UIActorWidgets, OpenScreensTickInOpenOrderOnTheUIPhase)
{
    NS::Scene scene;
    std::vector<std::string> log;
    OrderedUI front(log, "front", 1);
    OrderedUI back(log, "back", 0);
    front.Open(scene);
    back.Open(scene);

    scene.ExecutePhase(NS::UpdatePhase::UI);
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "front");
    EXPECT_EQ(log[1], "back");

    front.Close();
    log.clear();
    scene.ExecutePhase(NS::UpdatePhase::UI);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "back");
    back.Close();
}

// 描く順は DrawOrder の昇順で、同じ値は開いた順
TEST(UIActorWidgets, OpenScreensDrawInAscendingDrawOrder)
{
    NS::Scene scene;
    std::vector<std::string> log;
    OrderedUI front(log, "front", 1);
    OrderedUI back(log, "back", 0);
    OrderedUI frontLater(log, "frontLater", 1);
    front.Open(scene);
    back.Open(scene);
    frontLater.Open(scene);

    const std::vector<NS::UIActor*> order = scene.UI().DrawOrdered();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], &back);
    EXPECT_EQ(order[1], &front);
    EXPECT_EQ(order[2], &frontLater);
}

// シーンのアクターを組み直しても、開いている画面は開いたまま動く
TEST(UIActorWidgets, OpenScreenKeepsTickingAfterTheWorldIsRebuilt)
{
    NS::Scene scene;
    std::vector<std::string> log;
    OrderedUI ui(log, "ui", 0);
    ui.Open(scene);

    scene.ResetFromJson(NS::MakeSceneJson());

    EXPECT_TRUE(ui.IsOpen());
    scene.ExecutePhase(NS::UpdatePhase::UI);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "ui");
}

// UI の段の最中に開いた画面は、その歩では動かない
TEST(UIActorWidgets, ScreenOpenedDuringTheUIPhaseWaitsForTheNextStep)
{
    NS::Scene scene;
    std::vector<std::string> log;
    OrderedUI late(log, "late", 0);
    TogglingUI opener(scene, late, true);
    opener.Open(scene);

    scene.ExecutePhase(NS::UpdatePhase::UI);
    EXPECT_TRUE(late.IsOpen());
    EXPECT_TRUE(log.empty());

    scene.ExecutePhase(NS::UpdatePhase::UI);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "late");
}

// UI の段の最中に閉じた画面は、その歩の残りで動かない
TEST(UIActorWidgets, ScreenClosedDuringTheUIPhaseIsSkipped)
{
    NS::Scene scene;
    std::vector<std::string> log;
    OrderedUI victim(log, "victim", 0);
    TogglingUI closer(scene, victim, false);
    closer.Open(scene);
    victim.Open(scene);

    scene.ExecutePhase(NS::UpdatePhase::UI);

    EXPECT_FALSE(victim.IsOpen());
    EXPECT_TRUE(log.empty());
}

TEST(UIActorWidgets, WidgetsStretchToTheViewport)
{
    NS::Scene scene;
    NS::UIActor ui;
    ui.Widgets()->Root().AddChild<NS::UI::ColorRect>()->SetStretch(true);
    ui.Open(scene);
    ui.Widgets()->Layout(NS::UI::MakeUICanvas(1600.0f, 900.0f));
    EXPECT_NEAR(ui.Widgets()->Root().LayoutRect().width, 1920.0f, 0.0001f);
    EXPECT_FLOAT_EQ(ui.Widgets()->Root().LayoutRect().height, 1080.0f);
    ui.Close();
}

TEST(UIActorWidgets, CanvasScalesByTheViewportHeight)
{
    const NS::UI::UICanvas canvas = NS::UI::MakeUICanvas(1600.0f, 900.0f);
    EXPECT_FLOAT_EQ(canvas.scale, 900.0f / 1080.0f);
    EXPECT_NEAR(canvas.rect.width, 1920.0f, 0.0001f);
    EXPECT_FLOAT_EQ(canvas.rect.height, 1080.0f);

    const NS::UI::UICanvas empty = NS::UI::MakeUICanvas(0.0f, 900.0f);
    EXPECT_FLOAT_EQ(empty.scale, 1.0f);
    EXPECT_FLOAT_EQ(empty.rect.width, 0.0f);
    EXPECT_FLOAT_EQ(empty.rect.height, 0.0f);
}

// 拡縮と矩形は管理役が描く時に 1 つ出し、開いている画面は全部それで並ぶ
TEST(UIActorWidgets, DirectorLaysOutEveryOpenScreenWithOneCanvas)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "画面の拡縮の試し", .size = {64, 36}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    NS::Scene scene;
    NS::UIActor first;
    NS::UIActor second;
    first.Widgets()->Root().AddChild<NS::UI::ColorRect>()->SetStretch(true);
    second.Widgets()->Root().AddChild<NS::UI::ColorRect>()->SetStretch(true);
    first.Open(scene);
    second.Open(scene);
    // 拡縮と矩形だけを見るので、シェーダーの無い塗りで描かずに並べる
    NS::Gfx::ScreenOverlay overlay(&device, NS::Gfx::BuiltinShaders{});
    NS::Gfx::RenderContext context;
    context.device = &device;
    context.overlay = &overlay;

    device.BeginFrame();
    scene.UI().Draw(context);

    const NS::Size2D size = device.Size();
    const NS::UI::UICanvas expected =
        NS::UI::MakeUICanvas(static_cast<float>(size.width), static_cast<float>(size.height));
    EXPECT_FLOAT_EQ(scene.UI().Canvas().scale, expected.scale);
    EXPECT_FLOAT_EQ(first.Widgets()->Root().LayoutRect().width, expected.rect.width);
    EXPECT_FLOAT_EQ(second.Widgets()->Root().LayoutRect().width, expected.rect.width);
    EXPECT_FLOAT_EQ(first.Widgets()->Root().LayoutRect().height, 1080.0f);
    first.Close();
    second.Close();
}
