#include "Editor/MaterialParamFields.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/Material.h"

#include <gtest/gtest.h>

#include <imgui.h>

#include <functional>
#include <limits>

// マテリアルのシェーダーの値の欄へ打ち込んだ値が、.mat に書く値へどう入るかを縛る

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
            NS::uint8* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }

        ~HeadlessImGui() { ImGui::DestroyContext(m_context); }

        HeadlessImGui(const HeadlessImGui&) = delete;
        HeadlessImGui& operator=(const HeadlessImGui&) = delete;

        // 窓を 1 つ開いて欄を描く。focusFirst なら最初の欄を打ち込みの状態にする
        void Frame(const std::function<void()>& draw, bool focusFirst)
        {
            ImGui::NewFrame();
            ImGui::Begin("シェーダーの値");
            if (focusFirst)
            {
                ImGui::SetKeyboardFocusHere();
            }
            draw();
            ImGui::End();
            ImGui::EndFrame();
        }

        // 最初の欄に text を打ち込み、打ち込みが値へ効くまでフレームを回す
        void Type(const std::function<void()>& draw, const char* text)
        {
            // フォーカスの頼みは次のフレームの頭で効き、欄が打ち込みの状態になる。打つのはその後
            Frame(draw, true);
            Frame(draw, false);
            ImGui::GetIO().AddInputCharactersUTF8(text);
            for (int frame = 0; frame < 3; ++frame)
            {
                Frame(draw, false);
            }
        }

    private:
        ImGuiContext* m_context = nullptr;
    };
} // namespace

// 符号なしの欄へ負の数を打つと、大きな数へ回り込まずに下限の 0 で止まる
TEST(EditorMaterialParamFields, NegativeTextInUnsignedFieldStopsAtZero)
{
    HeadlessImGui gui;
    NS::MaterialParamValue value;
    value.numbers = {7.0};
    gui.Type([&] { (void)NS::Editor::DragParamInteger(0, std::numeric_limits<NS::uint32>::max(), value); }, "-5");
    EXPECT_EQ(value.numbers[0], 0.0);
}

// 浮動小数の欄で 1 つの成分を打ち直すと、その成分は打った表記のまま入り、触っていない成分は元の値のまま残る
TEST(EditorMaterialParamFields, TypingOneComponentLeavesTheOthers)
{
    HeadlessImGui gui;
    NS::MaterialParamValue value;
    value.numbers = {0.5, 0.1};
    gui.Type([&] { (void)NS::Editor::DragParamFloats(value); }, "0.3");
    EXPECT_EQ(value.numbers[0], 0.3);
    EXPECT_EQ(value.numbers[1], 0.1);
}
