#include "Editor/EditorUi.h"
#include "Editor/InspectorReflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/SubObjects/Model.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace
{
    class EditorColorTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            m_gui = ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2{1000.0f, 800.0f};
            io.DeltaTime = 1.0f / 60.0f;
            io.ConfigInputTrickleEventQueue = false;
            NS::uint8* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }

        void TearDown() override { ImGui::DestroyContext(m_gui); }

        NS::Editor::SubObjectEditResult Frame()
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
            ImGui::SetNextWindowSize(ImVec2{700.0f, 600.0f});
            ImGui::Begin("色の欄");
            m_tableMin = ImGui::GetCursorScreenPos();
            m_tableWidth = ImGui::GetContentRegionAvail().x;
            const NS::Editor::SubObjectEditResult result =
                NS::Editor::DrawReflectedSubObject(m_model, {}, nullptr, "基本色");
            ImGui::End();
            ImGui::EndFrame();
            return result;
        }

        void OpenPicker()
        {
            for (int i = 0; i < 3; ++i)
            {
                (void)Frame();
            }
            const ImGuiStyle& style = ImGui::GetStyle();
            const float x = m_tableMin.x + m_tableWidth - style.CellPadding.x -
                            NS::Editor::RevertButtonWidth() - style.ItemSpacing.x - ImGui::GetFrameHeight() * 0.5f;
            const float y = m_tableMin.y + style.CellPadding.y + ImGui::GetFrameHeight() * 0.5f;
            ImGui::GetIO().AddMousePosEvent(x, y);
            ImGui::GetIO().AddMouseButtonEvent(0, true);
            m_openActivated = Frame().activated;
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            const NS::Editor::SubObjectEditResult result = Frame();
            m_openActivated |= result.activated;
            m_openCommitted = result.committed;
        }

        ImGuiContext* m_gui = nullptr;
        NS::Model m_model{nullptr};
        ImVec2 m_tableMin{};
        float m_tableWidth = 0.0f;
        bool m_openActivated = false;
        bool m_openCommitted = false;
    };

    struct ColorSettings
    {
        NS::Color tint{0.1f, 0.2f, 0.3f, 0.4f};
        NS_REFLECT_BEGIN(ColorSettings, void)
        NS_REFLECT_FIELD(tint, "色合い")
        NS_REFLECT_END_VALUE()
    };
}

TEST(ColorField, ColorMemberIsAColorFieldSavedWithItsAlpha)
{
    const NS::ReflectionInfo& info = *ColorSettings::StaticReflection();
    const NS::FieldDesc* field = NS::FindField(&info, "色合い");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, NS::FieldType::Color);

    const ColorSettings settings;
    const nlohmann::json saved = NS::SerializeReflectedFields(&settings, info);
    EXPECT_EQ(saved["色合い"], nlohmann::json::array({0.1f, 0.2f, 0.3f, 0.4f}));
    ColorSettings loaded;
    loaded.tint = NS::Color{1.0f, 1.0f, 1.0f, 1.0f};
    EXPECT_EQ(NS::ApplyReflectedFields(&loaded, info, saved), 0u);
    EXPECT_TRUE(loaded.tint == settings.tint);
}

// 透明度の無い 3 成分は前の形の色。黙って既定に戻さず、警告して今の値を残す
TEST(ColorField, ThreeComponentColorWarnsAndKeepsTheValue)
{
    const NS::ReflectionInfo& info = *ColorSettings::StaticReflection();
    ColorSettings settings;
    const TestLogCapture log;
    (void)NS::ApplyReflectedFields(&settings, info, nlohmann::json{{"色合い", {1.0f, 1.0f, 1.0f}}});
    EXPECT_EQ(log.WarningCount(), 1);
    EXPECT_TRUE(settings.tint == (NS::Color{0.1f, 0.2f, 0.3f, 0.4f}));
}

TEST_F(EditorColorTest, ColorSwatchOpensAPickerBeforeChangingTheValue)
{
    m_model.SetBaseColor(NS::Color{0.2f, 0.4f, 0.7f});
    const nlohmann::json before = NS::SerializeSubObjectFields(m_model);
    OpenPicker();
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
    EXPECT_TRUE(m_openActivated);
    EXPECT_FALSE(m_openCommitted);
    EXPECT_EQ(NS::SerializeSubObjectFields(m_model), before);
}

TEST_F(EditorColorTest, DrawingAColorFieldPreservesHighIntensityAndSavedNumbers)
{
    m_model.SetBaseColor(NS::Color{2.5f, 0.3f, 0.1f});
    const nlohmann::json before = NS::SerializeSubObjectFields(m_model);
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_FALSE(Frame().changed);
    }
    EXPECT_EQ(NS::SerializeSubObjectFields(m_model), before);
    ASSERT_TRUE(before.contains("基本色"));
    EXPECT_FLOAT_EQ(before["基本色"][0].get<float>(), 2.5f);
}

TEST_F(EditorColorTest, ChoosingAColorWritesTheFieldAndCommitsWhenThePickerCloses)
{
    m_model.SetBaseColor(NS::Color{0.2f, 0.4f, 0.7f});
    const nlohmann::json before = NS::SerializeSubObjectFields(m_model);
    OpenPicker();
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
    (void)Frame();
    const ImGuiWindow* picker = GImGui->OpenPopupStack[0].Window;
    ASSERT_NE(picker, nullptr);
    const ImVec2 point{picker->DC.CursorStartPos.x + 40.0f, picker->DC.CursorStartPos.y + 40.0f};
    ImGui::GetIO().AddMousePosEvent(point.x, point.y);
    (void)Frame();
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    const NS::Editor::SubObjectEditResult press = Frame();
    EXPECT_TRUE(press.changed);
    EXPECT_FALSE(press.committed);
    EXPECT_EQ(press.changedTarget, &m_model);
    ASSERT_NE(press.changedField, nullptr);
    EXPECT_STREQ(press.changedField->name, "基本色");
    ImGui::GetIO().AddMousePosEvent(point.x + 25.0f, point.y + 15.0f);
    EXPECT_FALSE(Frame().committed);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    EXPECT_FALSE(Frame().committed);
    EXPECT_NE(NS::SerializeSubObjectFields(m_model), before);
    ImGui::GetIO().AddMousePosEvent(950.0f, 750.0f);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    bool committed = Frame().committed;
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    committed |= Frame().committed;
    committed |= Frame().committed;
    EXPECT_TRUE(committed);
    EXPECT_EQ(GImGui->OpenPopupStack.Size, 0);
}
