#include "Runtime/Graphics/Renderer.h"
#include "Runtime/Platform/Window.h"
#include "Runtime/UI/ImGuiContext.h"
#include "Runtime/UI/Panel.h"

#include <gtest/gtest.h>

TEST(UIPanelTest, NoOpWhenImGuiNotInitialized)
{
    // ImGuiContext を構築せずに Panel を作っても crash せず、 IsOpen は false
    NS::UI::Panel panel("test");
    EXPECT_FALSE(panel.IsOpen());
}

TEST(UIPanelTest, BeginEndPairInValidContext)
{
#if !NS_EDITOR_ENABLED
    GTEST_SKIP();
#endif
    NS::Platform::WindowDesc wd{};
    wd.visible = false;
    NS::Platform::Window window(wd);
    if (!window.IsValid())
        GTEST_SKIP();
    NS::Graphics::RendererDesc rd{};
    NS::Graphics::Renderer renderer(rd, window);
    if (!renderer.IsValid())
        GTEST_SKIP();
    NS::UI::ImGuiContext imgui(window, renderer);
    if (!imgui.IsValid())
        GTEST_SKIP() << "ImGui init 失敗";

    renderer.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    imgui.BeginFrame();
    {
        NS::UI::Panel panel("test panel");
        // デストラクタで End が呼ばれる、 ペアが揃わないと ImGui::EndFrame 内 assert で落ちる
    }
    imgui.EndFrame();
    renderer.EndFrame();
    SUCCEED();
}
