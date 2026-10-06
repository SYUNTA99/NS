#include "Editor/AssetsPanel.h"

#include "Editor/DragTypes.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "NSlib/Windows/Filesystem.h"

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
                {
                    selectionLabel = "オブジェクトを選択中";
                }
                ImGui::TextUnformatted(selectionLabel);

                ImGui::Separator();
            }

            RenderTree(NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), editor);
        }
        ImGui::End();
#else
        (void)editor;
#endif
    }

    void AssetsPanel::RenderTree(std::string_view dir, LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        const bool hasSelection = editor.HasGizmoSelection();

        // 開いた時だけ中身を走査する
        for (const std::string& sub : NS::OS::FileSystem::ListDirectories(dir))
        {
            const std::string label = NS::OS::FileSystem::FileName(sub);
            if (ImGui::TreeNode(label.c_str()))
            {
                RenderTree(sub, editor);
                ImGui::TreePop();
            }
        }

        for (const std::string& file : NS::OS::FileSystem::ListFiles(dir))
        {
            const std::string name = NS::OS::FileSystem::FileName(file);
            const std::string extension = NS::OS::FileSystem::Extension(file);

            if (extension == ".gltf" || extension == ".glb")
            {
                ImGui::PushID(name.c_str());
                ImGui::Selectable(name.c_str());
                if (ImGui::BeginDragDropSource())
                {
                    const std::string& full = file;
                    ImGui::SetDragDropPayload(k_MeshDragType, full.c_str(), full.size() + 1);
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Scene ビューへドラッグすると置ける");
                }
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
            {
                editor.ApplyMaterialToSelected(file);
            }
            if (ImGui::BeginDragDropSource())
            {
                const std::string& full = file;
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
