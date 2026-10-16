#include "Editor/DockController.h"
#include "Editor/PanelIds.h"
#include "NSlib/Windows/Filesystem.h"

#include <filesystem>
#include <functional>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace
{
    class EditorDockTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            m_context = ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.DisplaySize = ImVec2{1280.0f, 800.0f};
            io.DeltaTime = 1.0f / 60.0f;
            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }

        void TearDown() override
        {
            m_dock.ExitMaximize();
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::DestroyContext(m_context);
        }

        void Frame(const std::function<void()>& beforeHost = {})
        {
            ImGui::NewFrame();
            if (beforeHost)
            {
                beforeHost();
            }
            m_dock.RenderDockSpaceHost(30.0f);
            const char* panels[] = {NS::Editor::k_PanelScene, NS::Editor::k_PanelGame,
                                    NS::Editor::k_PanelActorList, NS::Editor::k_PanelInspector,
                                    NS::Editor::k_PanelConsole, NS::Editor::k_PanelAssets,
                                    NS::Editor::k_PanelEditMode, NS::Editor::k_PanelHitTimeline};
            for (const char* panel : panels)
            {
                if (m_dock.IsMaximized() && m_dock.MaximizedPanel() != panel)
                {
                    continue;
                }
                if (m_dock.IsMaximized())
                {
                    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
                    ImGui::SetNextWindowPos(ImVec2{0.0f, 30.0f});
                    ImGui::SetNextWindowSize(ImVec2{1280.0f, 770.0f});
                }
                ImGui::Begin(panel);
                ImGui::TextUnformatted("content");
                ImGui::End();
            }
            m_dock.RenderMaximizeButton();
            m_dock.TickTabFocus();
            ImGui::EndFrame();
        }

        ImGuiContext* m_context = nullptr;
        NS::Editor::DockController m_dock;
    };
}

TEST_F(EditorDockTest, TabBarsHaveNoOverlappingUtilityWindows)
{
    Frame();
    Frame();
    EXPECT_EQ(ImGui::FindWindowByName("##PanelMaxBtn0"), nullptr);
    EXPECT_EQ(ImGui::FindWindowByName("##PanelMaxBtn2"), nullptr);
}

TEST_F(EditorDockTest, EverySplitDirectionCanBeMergedBackIntoTheSameTabGroup)
{
    using Placement = NS::Editor::DockController::Placement;
    Frame();
    Frame();
    const Placement placements[] = {Placement::Left, Placement::Right, Placement::Above, Placement::Below};
    for (Placement placement : placements)
    {
        Frame([&]() { m_dock.MovePanel(NS::Editor::k_PanelGame, NS::Editor::k_PanelScene, placement); });
        Frame();
        const ImGuiWindow* scene = ImGui::FindWindowByName(NS::Editor::k_PanelScene);
        const ImGuiWindow* game = ImGui::FindWindowByName(NS::Editor::k_PanelGame);
        ASSERT_NE(scene->DockId, game->DockId);
        if (placement == Placement::Left)
        {
            EXPECT_LT(game->Pos.x, scene->Pos.x);
        }
        else if (placement == Placement::Right)
        {
            EXPECT_GT(game->Pos.x, scene->Pos.x);
        }
        else if (placement == Placement::Above)
        {
            EXPECT_LT(game->Pos.y, scene->Pos.y);
        }
        else
        {
            EXPECT_GT(game->Pos.y, scene->Pos.y);
        }
        Frame([&]() { m_dock.MovePanel(NS::Editor::k_PanelGame, NS::Editor::k_PanelScene, Placement::Tab); });
        Frame();
        EXPECT_EQ(scene->DockId, game->DockId);
    }
}

TEST_F(EditorDockTest, FloatingPanelCanHostAnotherPanelAndResetReturnsBothToTheEditor)
{
    using Placement = NS::Editor::DockController::Placement;
    Frame();
    Frame();
    Frame([&]() { m_dock.MovePanel(NS::Editor::k_PanelInspector, nullptr, Placement::Floating); });
    Frame();
    EXPECT_EQ(ImGui::FindWindowByName(NS::Editor::k_PanelInspector)->DockId, 0u);
    Frame([&]() { m_dock.MovePanel(NS::Editor::k_PanelAssets, NS::Editor::k_PanelInspector, Placement::Below); });
    Frame();
    const ImGuiWindow* inspector = ImGui::FindWindowByName(NS::Editor::k_PanelInspector);
    const ImGuiWindow* assets = ImGui::FindWindowByName(NS::Editor::k_PanelAssets);
    ASSERT_NE(inspector->DockNode, nullptr);
    ASSERT_NE(assets->DockNode, nullptr);
    EXPECT_EQ(ImGui::DockNodeGetRootNode(inspector->DockNode), ImGui::DockNodeGetRootNode(assets->DockNode));
    Frame([&]() { m_dock.ResetLayout(); });
    Frame();
    EXPECT_TRUE(ImGui::DockNodeGetRootNode(inspector->DockNode)->IsDockSpace());
    EXPECT_TRUE(ImGui::DockNodeGetRootNode(assets->DockNode)->IsDockSpace());
}

TEST_F(EditorDockTest, HiddenDockspaceKeepsCustomSplitsAlive)
{
    Frame();
    Frame();
    Frame([&]() { m_dock.MovePanel(NS::Editor::k_PanelGame, NS::Editor::k_PanelScene,
                                 NS::Editor::DockController::Placement::Right); });
    Frame();
    const ImGuiID sceneId = ImGui::FindWindowByName(NS::Editor::k_PanelScene)->DockId;
    const ImGuiID gameId = ImGui::FindWindowByName(NS::Editor::k_PanelGame)->DockId;
    for (int i = 0; i < 5; ++i)
    {
        ImGui::NewFrame();
        m_dock.RenderDockSpaceHost(0.0f, true);
        ImGui::EndFrame();
    }
    Frame();
    EXPECT_EQ(ImGui::FindWindowByName(NS::Editor::k_PanelScene)->DockId, sceneId);
    EXPECT_EQ(ImGui::FindWindowByName(NS::Editor::k_PanelGame)->DockId, gameId);
}

TEST_F(EditorDockTest, MaximizeRestoresCustomDockingAndSuspendsAutomaticSaving)
{
    Frame();
    Frame();
    Frame([&]() { m_dock.MovePanel(NS::Editor::k_PanelHitTimeline, NS::Editor::k_PanelScene,
                                 NS::Editor::DockController::Placement::Below); });
    Frame();
    const ImGuiID originalId = ImGui::FindWindowByName(NS::Editor::k_PanelHitTimeline)->DockId;
    ImGui::GetIO().IniFilename = "unused-editor-dock-test.ini";
    Frame([&]() { m_dock.EnterMaximize(NS::Editor::k_PanelHitTimeline); });
    Frame();
    EXPECT_TRUE(m_dock.IsMaximized());
    EXPECT_EQ(ImGui::GetIO().IniFilename, nullptr);
    EXPECT_EQ(ImGui::FindWindowByName(NS::Editor::k_PanelHitTimeline)->DockId, 0u);
    Frame([&]() { m_dock.ExitMaximize(); });
    Frame();
    EXPECT_FALSE(m_dock.IsMaximized());
    EXPECT_STREQ(ImGui::GetIO().IniFilename, "unused-editor-dock-test.ini");
    EXPECT_EQ(ImGui::FindWindowByName(NS::Editor::k_PanelHitTimeline)->DockId, originalId);
}

TEST_F(EditorDockTest, SavedLayoutSurvivesANewContextAndRejectsAnInvalidFile)
{
    Frame();
    Frame();
    Frame([&]() { m_dock.MovePanel(NS::Editor::k_PanelGame, NS::Editor::k_PanelScene,
                                 NS::Editor::DockController::Placement::Right); });
    Frame();
    const ImGuiID originalId = ImGui::FindWindowByName(NS::Editor::k_PanelGame)->DockId;
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("ns-editor-dock-" + std::to_string(reinterpret_cast<std::size_t>(m_context)));
    const std::string path = (directory / "layout.ini").string();
    ASSERT_TRUE(m_dock.SaveLayout(path.c_str()));
    Frame([&]() { m_dock.EnterMaximize(NS::Editor::k_PanelGame); });
    Frame();
    ASSERT_TRUE(m_dock.SaveLayout(path.c_str()));
    m_dock.ExitMaximize();
    ImGui::DestroyContext(m_context);
    SetUp();
    Frame([&]() { ASSERT_TRUE(m_dock.LoadLayout(path.c_str())); });
    Frame();
    EXPECT_EQ(ImGui::FindWindowByName(NS::Editor::k_PanelGame)->DockId, originalId);
    EXPECT_NE(ImGui::FindWindowByName(NS::Editor::k_PanelScene)->DockId, originalId);
    const std::string invalid = "invalid layout";
    ASSERT_TRUE(NS::OS::FileSystem::WriteAllBytes(path, std::as_bytes(std::span{invalid.data(), invalid.size()})));
    EXPECT_FALSE(m_dock.LoadLayout(path.c_str()));
    EXPECT_EQ(ImGui::FindWindowByName(NS::Editor::k_PanelGame)->DockId, originalId);
    std::filesystem::remove_all(directory);
}
