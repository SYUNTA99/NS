#include "Editor/ToolModePanel.h"

#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    void ToolModePanel::Render(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        // 固定座標を置くとドッキング配置と競合するので位置はドッキング / imgui.ini 任せ
        if (ImGui::Begin(k_PanelEditMode))
        {
            const bool objectActive = editor.ObjectToolActive();
            if (ImGui::RadioButton("Build (Grid place)", !objectActive))
                editor.SetObjectToolActive(false);
            if (ImGui::RadioButton("Object (Gizmo)", objectActive))
                editor.SetObjectToolActive(true);
            ImGui::Separator();
            if (objectActive)
                ImGui::TextUnformatted("Click orange box to select. Q/W/E/R = Select/Move/Rotate/Scale");
            else
                ImGui::TextUnformatted("Left click = place block");

            ImGui::Separator();
            ImGui::TextDisabled("F        選択物へ寄る");
            ImGui::TextDisabled("Del      選択物を削除");
            ImGui::TextDisabled("Ctrl+D   選択物を複製");
            ImGui::TextDisabled("Ctrl+S   上書き保存");
            ImGui::TextDisabled("F2       ヒエラルキーで改名");
            ImGui::TextDisabled("Ctrl/Shift クリックで複数選択");
        }
        ImGui::End();
#else
        (void)editor;
#endif
    }
} // namespace NS::Editor
