#include "Editor/EditorUi.h"
#include "Editor/InspectorReflection.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>

namespace
{
    struct CurveSettings
    {
        NS::Curve curve{};

        NS_REFLECT_BEGIN(CurveSettings, void)
        NS_FIELD(curve, "曲線")
        NS_REFLECT_END_VALUE()
    };

    class EditorCurveTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            m_gui = ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2{1100.0f, 850.0f};
            io.DeltaTime = 1.0f / 60.0f;
            io.ConfigInputTrickleEventQueue = false;
            NS::uint8* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            m_settings.curve.count = 3;
            m_settings.curve.keys[0] = NS::Curve::Key{0.0f, 0.0f};
            m_settings.curve.keys[1] = NS::Curve::Key{0.5f, 0.5f};
            m_settings.curve.keys[2] = NS::Curve::Key{1.0f, 1.0f};
            for (int i = 0; i < 3; ++i)
            {
                (void)Frame();
            }
        }

        void TearDown() override { ImGui::DestroyContext(m_gui); }

        NS::Editor::ValueEditResult Frame()
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
            ImGui::SetNextWindowSize(ImVec2{760.0f, 720.0f});
            ImGui::Begin("カーブの欄");
            m_tableMin = ImGui::GetCursorScreenPos();
            m_tableWidth = ImGui::GetContentRegionAvail().x;
            const NS::Editor::ValueEditResult result = NS::Editor::DrawReflectedValue(m_settings);
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            ImGui::PushOverrideID(ImGui::GetID("##value-fields"));
            ImGui::PushID(0);
            m_xMin = window->StateStorage.GetFloat(ImGui::GetID("graph-view-x-min"), 0.0f);
            m_xMax = window->StateStorage.GetFloat(ImGui::GetID("graph-view-x-max"), 1.0f);
            m_yMin = window->StateStorage.GetFloat(ImGui::GetID("graph-view-y-min"), -0.1f);
            m_yMax = window->StateStorage.GetFloat(ImGui::GetID("graph-view-y-max"), 1.1f);
            m_selected = window->StateStorage.GetInt(ImGui::GetID("graph-selected"), -1);
            ImGui::PopID();
            ImGui::PopID();
            m_graph = GraphRect(*window);
            m_controls.clear();
            m_keyButtons.clear();
            const ImU32 background = ImGui::GetColorU32(ImGuiCol_FrameBg);
            const ImU32 button = ImGui::GetColorU32(ImGuiCol_Button);
            for (int i = 0; i + 3 < window->DrawList->VtxBuffer.Size; ++i)
            {
                const ImDrawVert& first = window->DrawList->VtxBuffer[i];
                const ImDrawVert& opposite = window->DrawList->VtxBuffer[i + 2];
                if (first.col == button && opposite.col == button && first.pos.y > m_graph.Max.y &&
                    opposite.pos.x - first.pos.x == ImGui::GetFrameHeight() &&
                    opposite.pos.y - first.pos.y == ImGui::GetFrameHeight())
                {
                    m_keyButtons.push_back(ImRect{first.pos, opposite.pos});
                    i += 3;
                    continue;
                }
                if (first.col == background && opposite.col == background &&
                    opposite.pos.x - first.pos.x > 20.0f &&
                    opposite.pos.y - first.pos.y == ImGui::GetFrameHeight() && first.pos.y > m_graph.Max.y)
                {
                    m_controls.push_back(ImRect{first.pos, opposite.pos});
                    i += 3;
                }
            }
            ImGui::End();
            ImGui::EndFrame();
            return result;
        }

        ImRect GraphRect(const ImGuiWindow& window) const
        {
            const ImU32 background = ImGui::GetColorU32(ImGuiCol_FrameBg);
            for (int i = 0; i + 3 < window.DrawList->VtxBuffer.Size; ++i)
            {
                const ImDrawVert& first = window.DrawList->VtxBuffer[i];
                const ImDrawVert& opposite = window.DrawList->VtxBuffer[i + 2];
                if (first.col == background && opposite.col == background &&
                    opposite.pos.x - first.pos.x > 100.0f && opposite.pos.y - first.pos.y >= 95.0f)
                {
                    return ImRect{first.pos, opposite.pos};
                }
            }
            return ImRect{};
        }

        ImVec2 Point(float x, float y) const
        {
            return ImVec2{m_graph.Min.x + (x - m_xMin) / (m_xMax - m_xMin) * m_graph.GetWidth(),
                          m_graph.Max.y - (y - m_yMin) / (m_yMax - m_yMin) * m_graph.GetHeight()};
        }

        void MoveMouse(ImVec2 point)
        {
            ImGui::GetIO().AddMousePosEvent(point.x, point.y);
            (void)Frame();
        }

        ImGuiContext* m_gui = nullptr;
        CurveSettings m_settings{};
        ImRect m_graph{};
        ImVec2 m_tableMin{};
        float m_tableWidth = 0.0f;
        float m_xMin = 0.0f;
        float m_xMax = 1.0f;
        float m_yMin = -0.1f;
        float m_yMax = 1.1f;
        std::vector<ImRect> m_controls;
        std::vector<ImRect> m_keyButtons;
        int m_selected = -1;
    };
}

TEST_F(EditorCurveTest, ZoomPanAndFrameChangeOnlyTheView)
{
    const NS::Curve before = m_settings.curve;
    const float initialXMin = m_xMin;
    const float initialXMax = m_xMax;
    const float initialYMin = m_yMin;
    const ImVec2 anchor = Point(0.3f, 0.7f);
    MoveMouse(anchor);
    ImGui::GetIO().AddMouseWheelEvent(0.0f, 2.0f);
    EXPECT_FALSE(Frame().changed);
    EXPECT_LT(m_xMax - m_xMin, initialXMax - initialXMin);
    EXPECT_NEAR(Point(0.3f, 0.7f).x, anchor.x, 0.6f);
    EXPECT_NEAR(Point(0.3f, 0.7f).y, anchor.y, 0.6f);
    const float zoomedMin = m_xMin;
    ImGui::GetIO().AddMousePosEvent(anchor.x + 10.0f, anchor.y + 5.0f);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
    (void)Frame();
    EXPECT_FLOAT_EQ(m_xMin, zoomedMin);
    ImGui::GetIO().AddMousePosEvent(anchor.x + 35.0f, anchor.y + 20.0f);
    EXPECT_FALSE(Frame().changed);
    EXPECT_LT(m_xMin, zoomedMin);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
    (void)Frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_F, true);
    EXPECT_FALSE(Frame().changed);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_F, false);
    (void)Frame();
    EXPECT_FLOAT_EQ(m_xMin, initialXMin);
    EXPECT_FLOAT_EQ(m_xMax, initialXMax);
    EXPECT_FLOAT_EQ(m_yMin, initialYMin);
    EXPECT_EQ(m_settings.curve, before);
}

TEST_F(EditorCurveTest, DraggingKeepsTheViewStableAndCommitsOnRelease)
{
    const float initialYMin = m_yMin;
    const float initialYMax = m_yMax;
    MoveMouse(Point(0.5f, 0.5f));
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    EXPECT_TRUE(Frame().activated);
    const ImVec2 target = Point(0.65f, 1.8f);
    ImGui::GetIO().AddMousePosEvent(target.x, target.y);
    const NS::Editor::ValueEditResult moved = Frame();
    EXPECT_TRUE(moved.changed);
    EXPECT_FALSE(moved.committed);
    EXPECT_NEAR(m_settings.curve.keys[1].y, 1.8f, 0.01f);
    const float movedY = m_settings.curve.keys[1].y;
    (void)Frame();
    EXPECT_FLOAT_EQ(m_yMin, initialYMin);
    EXPECT_FLOAT_EQ(m_yMax, initialYMax);
    EXPECT_FLOAT_EQ(m_settings.curve.keys[1].y, movedY);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    EXPECT_TRUE(Frame().committed);
}

TEST_F(EditorCurveTest, ExpandedEditorStartsBeforeChangesAndCommitsWhenClosed)
{
    const NS::Curve before = m_settings.curve;
    const float x = m_tableMin.x + m_tableWidth * NS::Editor::k_FieldNameColumnRatio + 12.0f;
    const float y = m_tableMin.y + ImGui::GetStyle().CellPadding.y + ImGui::GetFrameHeight() * 0.5f;
    MoveMouse(ImVec2{x, y});
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    bool activated = Frame().activated;
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    const NS::Editor::ValueEditResult opened = Frame();
    activated |= opened.activated;
    EXPECT_TRUE(activated);
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
    EXPECT_FALSE(opened.committed);
    EXPECT_EQ(m_settings.curve, before);
    (void)Frame();
    ImGuiWindow* expanded = GImGui->OpenPopupStack[0].Window;
    ASSERT_NE(expanded, nullptr);
    EXPECT_GE(expanded->Size.x, 700.0f);
    const ImRect graph = GraphRect(*expanded);
    const ImVec2 key = graph.GetCenter();
    MoveMouse(key);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    (void)Frame();
    ImGui::GetIO().AddMousePosEvent(key.x + 30.0f, key.y - 30.0f);
    const NS::Editor::ValueEditResult edited = Frame();
    EXPECT_TRUE(edited.changed);
    EXPECT_FALSE(edited.committed);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    EXPECT_FALSE(Frame().committed);
    EXPECT_NE(m_settings.curve, before);
    const ImVec2 close{expanded->Pos.x + expanded->Size.x - ImGui::GetFrameHeight() * 0.5f,
                       expanded->Pos.y + ImGui::GetFrameHeight() * 0.5f};
    MoveMouse(close);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    (void)Frame();
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    bool committed = Frame().committed;
    committed |= Frame().committed;
    EXPECT_TRUE(committed);
    EXPECT_EQ(GImGui->OpenPopupStack.Size, 0);
}

TEST_F(EditorCurveTest, SelectedCoordinateInputKeepsSelectionAndTangentsWhenKeysReorder)
{
    m_settings.curve.keys[1].mode = NS::Curve::InterpMode::Manual;
    m_settings.curve.keys[1].inTangent = 2.0f;
    m_settings.curve.keys[1].outTangent = -1.0f;
    MoveMouse(Point(0.5f, 0.5f));
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    (void)Frame();
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    (void)Frame();
    ASSERT_GE(m_controls.size(), 2);
    const ImVec2 numeric = m_controls[0].GetCenter();
    MoveMouse(numeric);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    EXPECT_TRUE(Frame().activated);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    (void)Frame();
    ImGui::GetIO().AddInputCharactersUTF8("1.25");
    EXPECT_TRUE(Frame().changed);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    (void)Frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    (void)Frame();
    EXPECT_FLOAT_EQ(m_settings.curve.keys[2].x, 1.25f);
    EXPECT_EQ(m_settings.curve.keys[2].mode, NS::Curve::InterpMode::Manual);
    EXPECT_FLOAT_EQ(m_settings.curve.keys[2].inTangent, 2.0f);
    EXPECT_FLOAT_EQ(m_settings.curve.keys[2].outTangent, -1.0f);
    EXPECT_FLOAT_EQ(m_settings.curve.keys[1].x, 1.0f);
}

TEST_F(EditorCurveTest, InterpolationChoiceKeepsTheEditOpenUntilTheMenuCloses)
{
    MoveMouse(Point(0.5f, 0.5f));
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    (void)Frame();
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    (void)Frame();
    ASSERT_GE(m_controls.size(), 3);
    MoveMouse(m_controls[2].GetCenter());
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    bool activated = Frame().activated;
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    const NS::Editor::ValueEditResult opened = Frame();
    activated |= opened.activated;
    EXPECT_TRUE(activated);
    EXPECT_FALSE(opened.committed);
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
    (void)Frame();
    const ImGuiWindow* menu = GImGui->OpenPopupStack[0].Window;
    ASSERT_NE(menu, nullptr);
    const ImVec2 smooth{menu->DC.CursorStartPos.x + 30.0f,
                       menu->DC.CursorStartPos.y + ImGui::GetTextLineHeightWithSpacing() * 1.5f};
    MoveMouse(smooth);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    (void)Frame();
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    const NS::Editor::ValueEditResult chosen = Frame();
    EXPECT_TRUE(chosen.changed);
    bool committed = chosen.committed;
    committed |= Frame().committed;
    EXPECT_TRUE(committed);
    EXPECT_EQ(m_settings.curve.keys[1].mode, NS::Curve::InterpMode::AutoSmooth);
    EXPECT_EQ(GImGui->OpenPopupStack.Size, 0);
}

TEST_F(EditorCurveTest, ZoomingFarFromZeroRetainsAFiniteUsableRange)
{
    m_settings.curve.keys[0].x = 9999.0f;
    m_settings.curve.keys[1].x = 10000.0f;
    m_settings.curve.keys[2].x = 10001.0f;
    const NS::Curve before = m_settings.curve;
    MoveMouse(m_graph.GetCenter());
    ImGui::GetIO().AddKeyEvent(ImGuiKey_F, true);
    (void)Frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_F, false);
    (void)Frame();
    MoveMouse(Point(10000.0f, 0.5f));
    for (int i = 0; i < 12; ++i)
    {
        ImGui::GetIO().AddMouseWheelEvent(0.0f, 10.0f);
        EXPECT_FALSE(Frame().changed);
        ASSERT_GT(m_xMax, m_xMin);
        ASSERT_GT(m_yMax, m_yMin);
        ASSERT_TRUE(std::isfinite(m_xMin));
        ASSERT_TRUE(std::isfinite(m_xMax));
    }
    EXPECT_EQ(m_settings.curve, before);
}

TEST_F(EditorCurveTest, NumberedButtonsSelectAKeyWithoutChangingTheCurve)
{
    const NS::Curve before = m_settings.curve;
    ASSERT_EQ(m_keyButtons.size(), 3);
    MoveMouse(m_keyButtons[1].GetCenter());
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    EXPECT_FALSE(Frame().changed);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    EXPECT_FALSE(Frame().changed);
    EXPECT_EQ(m_selected, 1);
    EXPECT_EQ(m_settings.curve, before);
    EXPECT_GE(m_controls.size(), 3);
}

TEST_F(EditorCurveTest, AVisiblePointCanBeGrabbedAwayFromItsTinyCenter)
{
    const ImVec2 center = Point(0.5f, 0.5f);
    MoveMouse(ImVec2{center.x, center.y - 8.0f});
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    (void)Frame();
    EXPECT_EQ(m_selected, 1);
    ImGui::GetIO().AddMousePosEvent(center.x + 20.0f, center.y - 28.0f);
    EXPECT_TRUE(Frame().changed);
    EXPECT_GT(m_settings.curve.keys[1].y, 0.5f);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    EXPECT_TRUE(Frame().committed);
}

TEST_F(EditorCurveTest, ACurveBendHandleChangesTheShapeWithoutMovingTheKey)
{
    const NS::Curve before = m_settings.curve;
    const ImVec2 center = Point(0.5f, 0.5f);
    MoveMouse(center);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    (void)Frame();
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    (void)Frame();
    const float dx = m_graph.GetWidth() / (m_xMax - m_xMin);
    const float dy = -m_graph.GetHeight() / (m_yMax - m_yMin);
    const float length = std::sqrt(dx * dx + dy * dy);
    const ImVec2 tip{center.x + dx / length * 40.0f, center.y + dy / length * 40.0f};
    MoveMouse(tip);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    EXPECT_TRUE(Frame().activated);
    ImGui::GetIO().AddMousePosEvent(tip.x, tip.y - 25.0f);
    EXPECT_TRUE(Frame().changed);
    EXPECT_EQ(m_settings.curve.keys[1].mode, NS::Curve::InterpMode::Manual);
    EXPECT_FLOAT_EQ(m_settings.curve.keys[1].x, before.keys[1].x);
    EXPECT_FLOAT_EQ(m_settings.curve.keys[1].y, before.keys[1].y);
    EXPECT_NE(m_settings.curve.Evaluate(0.75f), before.Evaluate(0.75f));
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    EXPECT_TRUE(Frame().committed);
}
