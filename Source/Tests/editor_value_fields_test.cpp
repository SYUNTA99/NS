#include "Editor/InspectorReflection.h"
#include "Game/Level/HitTimeline.h"

#include <gtest/gtest.h>

#include <imgui.h>

// 部品でない値型 (タイムラインの事象) の欄を、Inspector と同じウィジェットで描いて書き換えられる事を縛る

namespace
{
    // 画面を持たない ImGui の文脈。フォントを焼き、画面の大きさを決めて 1 フレームずつ回す
    class HeadlessImGui
    {
    public:
        HeadlessImGui()
        {
            m_context = ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2{800.0f, 600.0f};
            io.DeltaTime = 1.0f / 60.0f;
            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }

        ~HeadlessImGui() { ImGui::DestroyContext(m_context); }

        HeadlessImGui(const HeadlessImGui&) = delete;
        HeadlessImGui& operator=(const HeadlessImGui&) = delete;

        // 窓を 1 つ開いて事象の欄を描く。focusFirst なら最初の欄を打ち込みの状態にする
        NS::Editor::ValueEditResult Frame(NS::Game::Level::FlashEvent& flash, bool focusFirst)
        {
            ImGui::NewFrame();
            ImGui::Begin("値の欄");
            if (focusFirst)
            {
                ImGui::SetKeyboardFocusHere();
            }
            const NS::Editor::ValueEditResult result = NS::Editor::DrawReflectedValue(flash);
            ImGui::End();
            ImGui::EndFrame();
            return result;
        }

    private:
        ImGuiContext* m_context = nullptr;
    };
} // namespace

// R-8: 事象の欄 (白の濃さ) へ打ち込むと、値が書き換わり、編集した欄が返る
TEST(EditorValueFields, TypingIntoAnEventFieldWritesTheValue)
{
    HeadlessImGui gui;
    NS::Game::Level::FlashEvent flash;
    flash.alpha = 0.5f;

    // フォーカスの頼みは次のフレームの頭で効き、欄が打ち込みの状態になる。打つのはその後
    (void)gui.Frame(flash, true);
    (void)gui.Frame(flash, false);
    ImGui::GetIO().AddInputCharactersUTF8("0.25");
    bool changed = false;
    const NS::Obj::FieldDesc* changedField = nullptr;
    for (int frame = 0; frame < 3; ++frame)
    {
        const NS::Editor::ValueEditResult result = gui.Frame(flash, false);
        if (result.changed)
        {
            changed = true;
            changedField = result.changedField;
        }
    }
    EXPECT_TRUE(changed);
    EXPECT_FLOAT_EQ(flash.alpha, 0.25f);
    ASSERT_NE(changedField, nullptr);
    EXPECT_STREQ(changedField->name, "濃さ");
}

// 何も触らないフレームは値を変えず、変えたとも返さない
TEST(EditorValueFields, DrawingAloneLeavesTheValue)
{
    HeadlessImGui gui;
    NS::Game::Level::FlashEvent flash;
    flash.alpha = 0.5f;
    const NS::Editor::ValueEditResult result = gui.Frame(flash, false);
    EXPECT_FALSE(result.changed);
    EXPECT_EQ(result.changedField, nullptr);
    EXPECT_FLOAT_EQ(flash.alpha, 0.5f);
}
