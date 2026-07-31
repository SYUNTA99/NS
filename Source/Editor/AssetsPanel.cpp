#include "Editor/AssetsPanel.h"

#include "Editor/DragTypes.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Runtime/Core/Filesystem.h"

#include <string>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    void AssetsPanel::Render(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        if (ImGui::Begin(k_PanelAssets))
        {
            // 適用にはギズモ選択が要るので枠は Object モード時だけ出す
            if (editor.ObjectToolActive())
            {
                const bool hasSelection = editor.HasGizmoSelection();
                const char* selectionLabel = "Select an object first (click it)";
                if (hasSelection)
                    selectionLabel = "Selected object: yes";
                ImGui::TextUnformatted(selectionLabel);

                // ドロップ枠: ツリーの .mat をここへドラッグすると選択中の物体へ適用する
                const char* dropLabel = "Drop target (needs selection)";
                if (hasSelection)
                    dropLabel = "Drop .mat here -> apply to selected";
                ImGui::Button(dropLabel, ImVec2(-1.0f, 32.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(k_MaterialDragType))
                    {
                        const char* droppedPath = static_cast<const char*>(payload->Data);
                        editor.ApplyMaterialToSelected(std::filesystem::path(droppedPath));
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::Separator();
            }

            // .mat はクリック適用 / ドラッグ可
            RenderTree(NS::Core::FileSystem::ContentRoot() / "Assets", editor);
        }
        ImGui::End();
#else
        (void)editor;
#endif
    }

    void AssetsPanel::RenderTree(const std::filesystem::path& dir, LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        const bool hasSelection = editor.HasGizmoSelection();

        // open 時のみ中身を走査する遅延読み
        for (const auto& sub : NS::Core::FileSystem::ListDirectories(dir))
        {
            const std::string label = sub.filename().string();
            if (ImGui::TreeNode(label.c_str()))
            {
                RenderTree(sub, editor);
                ImGui::TreePop();
            }
        }

        // フォルダ直下のファイル。 .mat はクリックで選択物体へ適用 + ドラッグ可、 他は読み取り専用表示
        for (const auto& file : NS::Core::FileSystem::ListFiles(dir))
        {
            const std::string name = file.filename().string();
            const std::filesystem::path extension = file.extension();

            // メッシュは Scene ビューへ落として置ける
            if (extension == ".gltf" || extension == ".glb")
            {
                ImGui::PushID(name.c_str());
                ImGui::Selectable(name.c_str());
                if (ImGui::BeginDragDropSource())
                {
                    const std::string full = file.string();
                    ImGui::SetDragDropPayload(k_MeshDragType, full.c_str(), full.size() + 1);
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Scene ビューへドラッグすると置ける");
                ImGui::PopID();
                continue;
            }

            if (extension != ".mat")
            {
                ImGui::TextDisabled("%s", name.c_str());
                continue;
            }
            ImGui::PushID(name.c_str());
            if (ImGui::Selectable(name.c_str()) && hasSelection)
                editor.ApplyMaterialToSelected(file);
            if (ImGui::BeginDragDropSource())
            {
                const std::string full = file.string();
                ImGui::SetDragDropPayload(k_MaterialDragType, full.c_str(), full.size() + 1);
                ImGui::TextUnformatted(name.c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }
#else
        (void)dir;
        (void)editor;
#endif
    }
} // namespace NS::Editor
