#include "Editor/InspectorReflection.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <string>
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
    NS::uint8* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    NS::ThirdPersonFollow follow{nullptr};
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
        const NS::Editor::ClassObjEditResult result =
            NS::Editor::DrawReflectedClassObj(follow, options, nullptr, "追従対象");
        ImGui::End();
        ImGui::EndFrame();
        return result;
    };
    const auto click = [&](ImVec2 at) {
        io.AddMousePosEvent(at.x, at.y);
        io.AddMouseButtonEvent(0, true);
        std::vector<NS::Editor::ClassObjEditResult> results{frame()};
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
    const std::vector<NS::Editor::ClassObjEditResult> results = click(option);
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

namespace
{
    class LabeledFieldObject final : public NS::Object
    {
    public:
        float speed = 2.0f;

        NS_REFLECT_BEGIN(LabeledFieldObject, NS::Object)
        NS_FIELD(speed, "speed", "走る速さ")
        NS_REFLECT_END()
    };
} // namespace

// 見せる名前を足した欄は、データの名前で保存されて読み戻され、インスペクタの見出しは見せる名前になる
TEST(EditorInspector, LabeledFieldSavesByNameAndShowsTheLabel)
{
    LabeledFieldObject object;
    const NS::ReflectionInfo& info = *object.GetReflection();
    EXPECT_EQ(NS::SerializeReflectedFields(&object, info), (nlohmann::json{{"speed", 2.0f}}));
    EXPECT_EQ(NS::ApplyReflectedFields(&object, info, nlohmann::json{{"speed", 5.0f}}), 0u);
    EXPECT_FLOAT_EQ(object.speed, 5.0f);

    ImGuiContext* gui = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    NS::uint8* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // 表の列の幅は数回描くと定まるので、最後のフレームの文字だけを写し取る
    std::string drawnText;
    for (int frame = 0; frame < 3; ++frame)
    {
        ImGui::NewFrame();
        ImGui::Begin("見せる名前の欄");
        const bool capture = frame == 2;
        if (capture)
        {
            ImGui::LogToBuffer();
        }
        (void)NS::Editor::DrawReflectedClassObj(object);
        if (capture)
        {
            drawnText = GImGui->LogBuffer.c_str();
            ImGui::LogFinish();
        }
        ImGui::End();
        ImGui::EndFrame();
    }
    ImGui::DestroyContext(gui);

    EXPECT_NE(drawnText.find("走る速さ"), std::string::npos) << drawnText;
    EXPECT_EQ(drawnText.find("speed"), std::string::npos) << drawnText;
}
