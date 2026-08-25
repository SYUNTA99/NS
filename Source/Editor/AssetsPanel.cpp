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
                const char* selectionLabel = "オブジェクトが未選択。クリックで選ぶ";
                if (hasSelection)
                    selectionLabel = "オブジェクトを選択中";
                ImGui::TextUnformatted(selectionLabel);

                const char* dropLabel = "選択してから .mat を落とす";
                if (hasSelection)
                    dropLabel = ".mat をここへ落とすと選択中のオブジェクトへ適用";
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

        // 開いた時だけ中身を走査する
        for (const auto& sub : NS::Core::FileSystem::ListDirectories(dir))
        {
            const std::string label = sub.filename().string();
            if (ImGui::TreeNode(label.c_str()))
            {
                RenderTree(sub, editor);
                ImGui::TreePop();
            }
        }

        for (const auto& file : NS::Core::FileSystem::ListFiles(dir))
        {
            const std::string name = file.filename().string();
            const std::filesystem::path extension = file.extension();

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
