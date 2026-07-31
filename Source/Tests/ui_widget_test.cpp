#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Platform/Window.h"
#include "Runtime/UI/UISystem.h"

#include <gtest/gtest.h>

/// 整形 (anchor / pivot / 拡縮) と入力の吸い込みは D3D 無しで検証する
/// 描画の呼び出し (実ピクセル矩形と実効 alpha) だけは headless renderer で確かめる

namespace
{
    // OnDraw で受け取った実ピクセル矩形と実効 alpha を控える検証用 Widget
    class ProbeWidget : public NS::UI::Widget
    {
    public:
        NS::UI::WidgetRect lastRectPx{};
        float lastAlpha = -1.0f;
        int drawCount = 0;

    protected:
        void OnDraw(NS::Graphics::Renderer&, const NS::UI::WidgetRect& rectPx, float alpha) override
        {
            lastRectPx = rectPx;
            lastAlpha = alpha;
            ++drawCount;
        }
    };
} // namespace

TEST(UIWidgetTest, AnchorPivotOffsetResolve)
{
    // 右上へ 20px 内側に貼る例。anchor が親の右上、pivot が自分の右上
    NS::UI::Widget widget;
    widget.SetAnchor({1.0f, 0.0f});
    widget.SetPivot({1.0f, 0.0f});
    widget.SetOffset({-20.0f, 20.0f});
    widget.SetSize({100.0f, 50.0f});

    const NS::UI::WidgetRect parent{0.0f, 0.0f, 1920.0f, 1080.0f};
    const NS::UI::WidgetRect rect = widget.ResolveRect(parent);

    EXPECT_FLOAT_EQ(rect.x, 1800.0f);
    EXPECT_FLOAT_EQ(rect.y, 20.0f);
    EXPECT_FLOAT_EQ(rect.width, 100.0f);
    EXPECT_FLOAT_EQ(rect.height, 50.0f);
}

TEST(UIWidgetTest, StretchFillsParent)
{
    NS::UI::Widget widget;
    widget.SetStretch(true);
    widget.SetOffset({500.0f, 500.0f});

    const NS::UI::WidgetRect parent{10.0f, 20.0f, 300.0f, 200.0f};
    const NS::UI::WidgetRect rect = widget.ResolveRect(parent);

    EXPECT_FLOAT_EQ(rect.x, 10.0f);
    EXPECT_FLOAT_EQ(rect.y, 20.0f);
    EXPECT_FLOAT_EQ(rect.width, 300.0f);
    EXPECT_FLOAT_EQ(rect.height, 200.0f);
}

TEST(UIWidgetTest, LayoutScalesFromViewportHeight)
{
    NS::UI::UISystem system;

    // 半分の解像度では拡縮 0.5、キャンバスは基準解像度のまま
    system.Layout(960.0f, 540.0f);
    EXPECT_FLOAT_EQ(system.Scale(), 0.5f);
    EXPECT_FLOAT_EQ(system.CanvasRect().width, 1920.0f);
    EXPECT_FLOAT_EQ(system.CanvasRect().height, 1080.0f);

    // 横長の画面では高さ基準のままキャンバスの横幅だけ伸びる
    system.Layout(2560.0f, 1080.0f);
    EXPECT_FLOAT_EQ(system.Scale(), 1.0f);
    EXPECT_FLOAT_EQ(system.CanvasRect().width, 2560.0f);
    EXPECT_FLOAT_EQ(system.CanvasRect().height, 1080.0f);
}

TEST(UIWidgetTest, LayoutRectPropagatesThroughTree)
{
    NS::UI::UISystem system;
    auto* panel = system.Root().AddChild<NS::UI::Widget>();
    panel->SetAnchor({0.5f, 0.5f});
    panel->SetPivot({0.5f, 0.5f});
    panel->SetSize({400.0f, 200.0f});
    auto* child = panel->AddChild<NS::UI::Widget>();
    child->SetSize({50.0f, 50.0f});

    system.Layout(1920.0f, 1080.0f);

    // 中央寄せの親は画面中央、子は親の左上が原点になる
    EXPECT_FLOAT_EQ(panel->LayoutRect().x, 760.0f);
    EXPECT_FLOAT_EQ(panel->LayoutRect().y, 440.0f);
    EXPECT_FLOAT_EQ(child->LayoutRect().x, 760.0f);
    EXPECT_FLOAT_EQ(child->LayoutRect().y, 440.0f);
}

TEST(UIWidgetTest, ConsumesPointerHonorsBlocksInputAndVisibility)
{
    NS::UI::UISystem system;
    auto* panel = system.Root().AddChild<NS::UI::Widget>();
    panel->SetSize({100.0f, 100.0f});
    panel->SetBlocksInput(true);
    system.Layout(1920.0f, 1080.0f);

    EXPECT_TRUE(system.ConsumesPointer(50.0f, 50.0f));
    EXPECT_FALSE(system.ConsumesPointer(150.0f, 50.0f));

    // 見えていない間は吸わない
    panel->SetVisible(false);
    EXPECT_FALSE(system.ConsumesPointer(50.0f, 50.0f));

    // 吸う札が無い Widget も素通しになる
    panel->SetVisible(true);
    panel->SetBlocksInput(false);
    EXPECT_FALSE(system.ConsumesPointer(50.0f, 50.0f));
}

TEST(UIWidgetTest, ConsumesPointerScalesFromViewport)
{
    NS::UI::UISystem system;
    auto* panel = system.Root().AddChild<NS::UI::Widget>();
    panel->SetSize({100.0f, 100.0f});
    panel->SetBlocksInput(true);

    // 半分の解像度では実ピクセルも半分の範囲が当たる
    system.Layout(960.0f, 540.0f);
    EXPECT_TRUE(system.ConsumesPointer(49.0f, 49.0f));
    EXPECT_FALSE(system.ConsumesPointer(51.0f, 49.0f));
}

TEST(UIWidgetTest, RenderDrawsScaledRectAndCascadedAlpha)
{
    NS::Platform::WindowDesc wd{};
    wd.visible = false;
    NS::Platform::Window window(wd);
    if (!window.IsValid())
        GTEST_SKIP();
    NS::Graphics::RendererDesc rd{};
    NS::Graphics::Renderer renderer(rd, window);
    if (!renderer.IsValid())
        GTEST_SKIP();

    NS::UI::UISystem system;
    auto* panel = system.Root().AddChild<NS::UI::Widget>();
    panel->SetStretch(true);
    panel->SetAlpha(0.5f);
    auto* probe = panel->AddChild<ProbeWidget>();
    probe->SetStretch(true);
    probe->SetAlpha(0.5f);

    renderer.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    system.Render(renderer);
    renderer.EndFrame();

    // alpha は木を掛け算で降り、矩形は実ピクセルへ拡縮されて届く
    ASSERT_EQ(probe->drawCount, 1);
    EXPECT_FLOAT_EQ(probe->lastAlpha, 0.25f);
    const NS::Math::Size2D size = renderer.Size();
    EXPECT_NEAR(probe->lastRectPx.width, static_cast<float>(size.width), 0.01f);
    EXPECT_NEAR(probe->lastRectPx.height, static_cast<float>(size.height), 0.01f);
}
