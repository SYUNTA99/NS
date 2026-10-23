#include "Editor/ToolModePanel.h"

#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS
{
    namespace Editor
    {
        void ToolModePanel::Render(LevelEditorController& editor) noexcept
        {
#if NS_EDITOR_ENABLED
            // 固定座標を置くとドッキング配置と競合するので位置はドッキング / imgui.ini 任せ
            if (ImGui::Begin(k_PanelEditMode))
            {
                const bool gizmoActive = editor.GizmoToolActive();
                if (ImGui::RadioButton("Build (Grid place)", !gizmoActive))
                {
                    editor.SetGizmoToolActive(false);
                }
                if (ImGui::RadioButton("Gizmo", gizmoActive))
                {
                    editor.SetGizmoToolActive(true);
                }
                ImGui::Separator();
                if (gizmoActive)
                {
                    ImGui::TextUnformatted("Click orange box to select. Q/W/E/R = Select/Move/Rotate/Scale");
                }
                else
                {
                    ImGui::TextUnformatted("Left click = place block");
                }

                // 感度はこの場で効き、シーンには保存しない
                ImGui::SeparatorText("自由視点");
                Editor::EditorCamera::FeelTuning& tuning = editor.EditorFreeCamera().Tuning();
                // 手入力も範囲に収める。非数は範囲で止まらないので EditorCamera が断る
                constexpr ImGuiSliderFlags k_Clamp = ImGuiSliderFlags_AlwaysClamp;
                ImGui::DragFloat("ズームバネ角速度", &tuning.springOmega, 0.1f, 0.5f, 30.0f, "%.3f", k_Clamp);
                ImGui::DragFloat(
                    "マウス見回し感度", &tuning.mouseSensOrbit, 0.0005f, 0.0005f, 0.02f, "%.4f", k_Clamp);
                ImGui::DragFloat("マウス平行移動感度", &tuning.mouseSensPan, 0.001f, 0.001f, 0.2f, "%.3f", k_Clamp);
                ImGui::DragFloat("マウスズーム感度", &tuning.mouseSensZoom, 0.05f, 0.1f, 5.0f, "%.3f", k_Clamp);
                ImGui::DragFloat("パッド見回し感度", &tuning.padSensOrbit, 0.05f, 0.1f, 10.0f, "%.3f", k_Clamp);
                ImGui::DragFloat("パッド平行移動感度", &tuning.padSensPan, 0.1f, 0.5f, 30.0f, "%.3f", k_Clamp);
                ImGui::DragFloat("パッドズーム感度", &tuning.padSensZoom, 0.05f, 0.5f, 15.0f, "%.3f", k_Clamp);
                ImGui::DragFloat("キー移動速度", &tuning.keyMoveSpeed, 0.02f, 0.05f, 3.0f, "%.3f", k_Clamp);
                ImGui::DragFloat("Shift の加速倍率", &tuning.boostMoveScale, 0.1f, 1.0f, 20.0f, "%.3f", k_Clamp);

                ImGui::Separator();
                ImGui::TextDisabled("F        選択物へ寄る");
                ImGui::TextDisabled("Del      選択物を削除");
                ImGui::TextDisabled("Ctrl+D   選択物を複製");
                ImGui::TextDisabled("Ctrl+S   上書き保存");
                ImGui::TextDisabled("F2       アクター一覧で改名");
                ImGui::TextDisabled("Ctrl/Shift クリックで複数選択");
            }
            ImGui::End();
#else
            (void)editor;
#endif
        }
    } // namespace Editor
} // namespace NS
