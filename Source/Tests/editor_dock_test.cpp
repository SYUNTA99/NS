#include "Editor/DockController.h"
#include "Editor/PanelIds.h"
#include "NSlib/Core/Windows/Filesystem.h"

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
                                    NS::Editor::k_PanelEditMode, NS::Editor::k_PanelActionTimeline};
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
            m_dock.RenderMaximizeButton(false);
            m_dock.TickTabFocus();
            ImGui::EndFrame();
        }

        void DragPanelTo(const char* panel, ImVec2 destination, bool floating = true)
        {
            if (floating)
            {
                Frame([&]() { m_dock.MovePanel(panel, nullptr, NS::Editor::DockController::Placement::Floating); });
                Frame();
            }
            const ImGuiWindow* window = ImGui::FindWindowByName(panel);
            ImVec2 start{window->Pos.x + 80.0f, window->Pos.y + ImGui::GetFrameHeight() * 0.5f};
            if (!floating)
            {
                ASSERT_NE(window->DockNode, nullptr);
                ImGuiTabBar* bar = window->DockNode->TabBar;
                ASSERT_NE(bar, nullptr);
                const ImGuiTabItem* tab = ImGui::TabBarFindTabByID(bar, window->TabId);
                ASSERT_NE(tab, nullptr);
                start = ImVec2{bar->BarRect.Min.x + tab->Offset + tab->Width * 0.5f, bar->BarRect.GetCenter().y};
            }
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(start.x, start.y);
            Frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            Frame();
            io.AddMousePosEvent(start.x + 30.0f, start.y + ImGui::GetFrameHeight() * 3.0f);
            Frame();
            io.AddMousePosEvent(destination.x, destination.y);
            Frame();
            Frame();
            ASSERT_NE(ImGui::GetDragDropPayload(), nullptr);
            ASSERT_TRUE(ImGui::GetDragDropPayload()->IsDataType(IMGUI_PAYLOAD_TYPE_WINDOW));
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            Frame();
            Frame();
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

TEST_F(EditorDockTest, DroppingIntoThePanelBodyMergesWithoutAimingAtAMarker)
{
    Frame();
    Frame();
    const ImGuiWindow* scene = ImGui::FindWindowByName(NS::Editor::k_PanelScene);
    const ImVec2 destination{scene->Pos.x + scene->Size.x * 0.62f, scene->Pos.y + scene->Size.y * 0.67f};
    DragPanelTo(NS::Editor::k_PanelAssets, destination);
    const ImGuiWindow* assets = ImGui::FindWindowByName(NS::Editor::k_PanelAssets);
    EXPECT_NE(assets->DockId, 0u);
    EXPECT_EQ(assets->DockId, scene->DockId);
}

TEST_F(EditorDockTest, DraggingADockedTabDirectlyIntoAnotherPanelBodyMergesIt)
{
    Frame();
    Frame();
    const ImGuiWindow* scene = ImGui::FindWindowByName(NS::Editor::k_PanelScene);
    const ImVec2 destination{scene->Pos.x + scene->Size.x * 0.62f, scene->Pos.y + scene->Size.y * 0.67f};
    DragPanelTo(NS::Editor::k_PanelAssets, destination, false);
    EXPECT_EQ(ImGui::FindWindowByName(NS::Editor::k_PanelAssets)->DockId, scene->DockId);
}

TEST_F(EditorDockTest, DroppingNearAPanelEdgeSplitsAwayFromTheOldMarker)
{
    const ImVec2 points[] = {{0.12f, 0.68f}, {0.88f, 0.32f}, {0.63f, 0.12f}, {0.37f, 0.88f}};
    for (int direction = 0; direction < IM_ARRAYSIZE(points); ++direction)
    {
        Frame([&]() { m_dock.ResetLayout(); });
        Frame();
        const ImGuiWindow* scene = ImGui::FindWindowByName(NS::Editor::k_PanelScene);
        const ImVec2 destination{scene->Pos.x + scene->Size.x * points[direction].x,
                                 scene->Pos.y + scene->Size.y * points[direction].y};
        DragPanelTo(NS::Editor::k_PanelAssets, destination);
        const ImGuiWindow* assets = ImGui::FindWindowByName(NS::Editor::k_PanelAssets);
        ASSERT_NE(assets->DockNode, nullptr);
        EXPECT_NE(assets->DockId, scene->DockId);
        if (direction == ImGuiDir_Left)
        {
            EXPECT_LT(assets->Pos.x, scene->Pos.x);
        }
        else if (direction == ImGuiDir_Right)
        {
            EXPECT_GT(assets->Pos.x, scene->Pos.x);
        }
        else if (direction == ImGuiDir_Up)
        {
            EXPECT_LT(assets->Pos.y, scene->Pos.y);
        }
        else
        {
            EXPECT_GT(assets->Pos.y, scene->Pos.y);
        }
        EXPECT_EQ(ImGui::DockNodeGetRootNode(assets->DockNode), ImGui::DockNodeGetRootNode(scene->DockNode));
    }
}

TEST_F(EditorDockTest, DroppingOntoATabBarMergesInsteadOfSplittingTheWholeLayout)
{
    Frame();
    Frame();
    const ImGuiWindow* scene = ImGui::FindWindowByName(NS::Editor::k_PanelScene);
    ASSERT_NE(scene->DockNode->TabBar, nullptr);
    const ImRect bar = scene->DockNode->TabBar->BarRect;
    DragPanelTo(NS::Editor::k_PanelAssets, ImVec2{bar.Min.x + bar.GetWidth() * 0.62f, bar.GetCenter().y});
    EXPECT_EQ(ImGui::FindWindowByName(NS::Editor::k_PanelAssets)->DockId, scene->DockId);
}

TEST_F(EditorDockTest, DroppingAlongTheWorkspaceEdgeSplitsTheWholeLayout)
{
    for (int direction = ImGuiDir_Left; direction < ImGuiDir_COUNT; ++direction)
    {
        Frame([&]() { m_dock.ResetLayout(); });
        Frame();
        const ImGuiWindow* scene = ImGui::FindWindowByName(NS::Editor::k_PanelScene);
        const ImGuiDockNode* root = ImGui::DockNodeGetRootNode(scene->DockNode);
        const ImVec2 size = root->Size;
        ImVec2 destination{root->Pos.x + size.x * 0.55f, root->Pos.y + size.y * 0.64f};
        if (direction == ImGuiDir_Left)
        {
            destination.x = root->Pos.x + 8.0f;
        }
        else if (direction == ImGuiDir_Right)
        {
            destination.x = root->Pos.x + size.x - 8.0f;
        }
        else if (direction == ImGuiDir_Up)
        {
            destination.y = root->Pos.y + ImGui::GetFrameHeight() + 8.0f;
        }
        else
        {
            destination.y = root->Pos.y + size.y - 8.0f;
        }
        DragPanelTo(NS::Editor::k_PanelAssets, destination);
        const ImGuiWindow* assets = ImGui::FindWindowByName(NS::Editor::k_PanelAssets);
        ASSERT_NE(assets->DockNode, nullptr);
        if (direction == ImGuiDir_Left || direction == ImGuiDir_Right)
        {
            EXPECT_NEAR(assets->DockNode->Size.y, size.y, 1.0f);
        }
        else
        {
            EXPECT_NEAR(assets->DockNode->Size.x, size.x, 1.0f);
        }
    }
}

TEST_F(EditorDockTest, DisablingSplitsStillAllowsBodyDropsToMerge)
{
    Frame();
    Frame();
    ImGui::GetIO().ConfigDockingNoSplit = true;
    const ImGuiWindow* scene = ImGui::FindWindowByName(NS::Editor::k_PanelScene);
    const ImVec2 destination{scene->Pos.x + scene->Size.x * 0.12f, scene->Pos.y + scene->Size.y * 0.68f};
    DragPanelTo(NS::Editor::k_PanelAssets, destination);
    EXPECT_EQ(ImGui::FindWindowByName(NS::Editor::k_PanelAssets)->DockId, scene->DockId);
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
    Frame([&]() { m_dock.MovePanel(NS::Editor::k_PanelActionTimeline, NS::Editor::k_PanelScene,
                                 NS::Editor::DockController::Placement::Below); });
    Frame();
    const ImGuiID originalId = ImGui::FindWindowByName(NS::Editor::k_PanelActionTimeline)->DockId;
    ImGui::GetIO().IniFilename = "unused-editor-dock-test.ini";
    Frame([&]() { m_dock.EnterMaximize(NS::Editor::k_PanelActionTimeline); });
    Frame();
    EXPECT_TRUE(m_dock.IsMaximized());
    EXPECT_EQ(ImGui::GetIO().IniFilename, nullptr);
    EXPECT_EQ(ImGui::FindWindowByName(NS::Editor::k_PanelActionTimeline)->DockId, 0u);
    Frame([&]() { m_dock.ExitMaximize(); });
    Frame();
    EXPECT_FALSE(m_dock.IsMaximized());
    EXPECT_STREQ(ImGui::GetIO().IniFilename, "unused-editor-dock-test.ini");
    EXPECT_EQ(ImGui::FindWindowByName(NS::Editor::k_PanelActionTimeline)->DockId, originalId);
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
