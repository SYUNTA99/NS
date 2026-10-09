#include "Editor/InspectorReflection.h"
#include "NSlib/Object/SubObjects/ThirdPersonFollow.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <vector>

// コンボで参照先を選ぶと、値が変わる前に編集の始まりを、変わった後に確定を返す
// 始まりが値の後だと、控えが変わった後の値になり undo に積まれない
TEST(EditorInspector, ComboSelectionStartsBeforeTheChangeAndCommitsAfter)
{
    ImGuiContext* gui = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    NS::Obj::ThirdPersonFollow follow;
    const std::vector<NS::Editor::ObjectRefOption> options{{7u, "相手"}};
    ImVec2 tableMin{};
    float tableWidth = 0.0f;
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
        ImGui::SetNextWindowSize(ImVec2{600.0f, 400.0f});
        ImGui::Begin("参照の欄");
        tableMin = ImGui::GetCursorScreenPos();
        tableWidth = ImGui::GetContentRegionAvail().x;
        const NS::Editor::SubObjectEditResult result =
            NS::Editor::DrawReflectedSubObject(follow, options, nullptr, "追従対象");
        ImGui::End();
        ImGui::EndFrame();
        return result;
    };
    const auto click = [&](ImVec2 at) {
        io.AddMousePosEvent(at.x, at.y);
        io.AddMouseButtonEvent(0, true);
        std::vector<NS::Editor::SubObjectEditResult> results{frame()};
        io.AddMouseButtonEvent(0, false);
        results.push_back(frame());
        return results;
    };

    // 検索で欄を 1 つに絞ったので、コンボは表の 1 行目の値の列にある。列の幅は数回描くと定まる
    for (int i = 0; i < 3; ++i)
    {
        (void)frame();
    }
    const float rowHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
    (void)click(ImVec2{tableMin.x + tableWidth * 0.6f, tableMin.y + rowHeight * 0.5f});
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
    (void)frame();

    const ImGuiWindow* popup = GImGui->OpenPopupStack[0].Window;
    ASSERT_NE(popup, nullptr);
    const float line = ImGui::GetTextLineHeight();
    const ImVec2 option{popup->Pos.x + popup->Size.x * 0.5f,
                        popup->Pos.y + popup->WindowPadding.y + line + ImGui::GetStyle().ItemSpacing.y + line * 0.5f};
    const std::vector<NS::Editor::SubObjectEditResult> results = click(option);
    ASSERT_EQ(follow.TargetRef().id, 7u);

    int activatedAt = -1;
    int changedAt = -1;
    int committedAt = -1;
    for (int i = 0; i < static_cast<int>(results.size()); ++i)
    {
        if (results[i].activated && activatedAt < 0)
        {
            activatedAt = i;
        }
        if (results[i].changed && changedAt < 0)
        {
            changedAt = i;
        }
        if (results[i].committed)
        {
            committedAt = i;
        }
    }
    EXPECT_GE(activatedAt, 0);
    EXPECT_LT(activatedAt, changedAt);
    EXPECT_GE(committedAt, changedAt);
    ImGui::DestroyContext(gui);
}
