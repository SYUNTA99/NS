#include "Framework/Graphics/Renderer.h"
#include "Framework/Platform/Window.h"
#include "Framework/UI/ImGuiContext.h"

#include <gtest/gtest.h>

namespace
{
    /// hidden window + Debug layer 無効化で軽量に Renderer を立ち上げる
    /// headless CI でも fail せず GTEST_SKIP に落とせるよう IsValid を後で確認する
    NS::Platform::WindowDesc MakeHiddenWindowDesc()
    {
        NS::Platform::WindowDesc desc{};
        desc.title = "imgui_context_test";
        desc.size = {320, 240};
        desc.visible = false;
        return desc;
    }
} // namespace

TEST(ImGuiContextTest, ValidModeWithRealWindowAndRenderer)
{
#if !NS_EDITOR_ENABLED
    GTEST_SKIP() << "ImGui 実機能は Debug / Development build のみ";
#endif
    NS::Platform::Window window(MakeHiddenWindowDesc());
    if (!window.IsValid())
        GTEST_SKIP() << "headless 環境で Window 構築失敗";

    NS::Graphics::RendererDesc rd{};
    rd.enableDebugLayer = false;
    NS::Graphics::Renderer renderer(rd, window);
    if (!renderer.IsValid())
        GTEST_SKIP() << "headless 環境で Renderer 構築失敗";

    NS::UI::ImGuiContext imgui(window, renderer);
    EXPECT_TRUE(imgui.IsValid());
    EXPECT_FALSE(imgui.IsUsingFallback());

    // BeginFrame / EndFrame ペアが crash しないことを確認
    renderer.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    imgui.BeginFrame();
    imgui.EndFrame();
    renderer.EndFrame();
}

TEST(ImGuiContextTest, WantCaptureSafeBeforeNewFrame)
{
#if !NS_EDITOR_ENABLED
    GTEST_SKIP();
#endif
    NS::Platform::Window window(MakeHiddenWindowDesc());
    if (!window.IsValid())
        GTEST_SKIP();
    NS::Graphics::RendererDesc rd{};
    NS::Graphics::Renderer renderer(rd, window);
    if (!renderer.IsValid())
        GTEST_SKIP();

    NS::UI::ImGuiContext imgui(window, renderer);
    if (!imgui.IsValid())
        GTEST_SKIP();

    // NewFrame 未呼出でも crash しないこと (初期値は false 想定だが ImGui 内部に依存)
    (void)imgui.WantCaptureMouse();
    (void)imgui.WantCaptureKeyboard();
    SUCCEED();
}
