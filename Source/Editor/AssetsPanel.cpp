#include "Editor/AssetsPanel.h"

#include "Editor/AssetPicker.h"
#include "Editor/AssetPreview.h"
#include "Editor/DragTypes.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "NSlib/App/Application.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/Gfx/D3DRenderTarget.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Windows/Filesystem.h"

#include <string>
#include <algorithm>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS
{
    namespace Editor
    {
        AssetsPanel::AssetsPanel() = default;
        AssetsPanel::~AssetsPanel() = default;

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

                RenderTree(OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), "Assets"), editor);
            }
            ImGui::End();
#else
            (void)editor;
#endif
        }

#if NS_EDITOR_ENABLED
        const Gfx::D3DTexture* AssetsPanel::LoadThumbnail(const std::string& file)
        {
            Application* app = Application::Get();
            if (app == nullptr || !app->IsValid())
            {
                return nullptr;
            }
            AssetManager& assets = AssetManager::Get();
            if (IsTextureAssetPath(file))
            {
                const Gfx::D3DTexture* image = assets.LoadTexture(file)->D3D();
                if (image->IsValid() && !image->IsUsingFallback())
                {
                    return image;
                }
                return nullptr;
            }
            Object* asset = nullptr;
            const std::string root = OS::FileSystem::Normalize(OS::FileSystem::ContentRoot());
            const std::string name = OS::FileSystem::Normalize(file).substr(root.size() + 1);
            if (OS::FileSystem::Extension(file) == ".mat")
            {
                asset = assets.LoadMaterial(name);
            }
            else
            {
                asset = assets.LoadMesh(name);
            }
            const std::optional<Gfx::DrawItem> item = MakeAssetPreviewItem(*asset);
            if (!item)
            {
                return nullptr;
            }
            std::unique_ptr<Gfx::D3DRenderTarget>& target = m_previews[file];
            if (!target)
            {
                target = Gfx::D3DRenderTarget::Create({80, 80});
            }
            if (!RenderAssetPreview(app->Device(), *target, *item))
            {
                return nullptr;
            }
            return target->Color();
        }

        void AssetsPanel::RenderTile(const std::string& file, LevelEditorController& editor, float width)
        {
            const std::string name = OS::FileSystem::FileName(file);
            const std::string extension = OS::FileSystem::Extension(file);
            const float thumb = std::min(80.0f, width);
            const float height = thumb + ImGui::GetTextLineHeight() * 2.0f + 10.0f;
            ImGui::PushID(file.c_str());
            const ImVec2 start = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##asset", editor.IsAssetSelected(file), ImGuiSelectableFlags_None,
                                   ImVec2{width, height}))
            {
                editor.SelectAsset(file);
            }
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemVisible())
            {
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const ImVec2 low{start.x + (width - thumb) * 0.5f, start.y};
                for (int y = 0; y < static_cast<int>(thumb); y += 8)
                {
                    for (int x = 0; x < static_cast<int>(thumb); x += 8)
                    {
                        ImU32 color = IM_COL32(55, 55, 55, 255);
                        if ((x / 8 + y / 8) % 2 == 0)
                        {
                            color = IM_COL32(80, 80, 80, 255);
                        }
                        draw->AddRectFilled(ImVec2{low.x + x, low.y + y},
                                            ImVec2{low.x + std::min(x + 8.0f, thumb),
                                                   low.y + std::min(y + 8.0f, thumb)}, color);
                    }
                }
                if (const Gfx::D3DTexture* image = LoadThumbnail(file))
                {
                    const Size2D size = image->Size();
                    const float scale = thumb / static_cast<float>(std::max(size.width, size.height));
                    const ImVec2 dimensions{size.width * scale, size.height * scale};
                    const ImVec2 at{low.x + (thumb - dimensions.x) * 0.5f,
                                    low.y + (thumb - dimensions.y) * 0.5f};
                    const ImTextureID handle = static_cast<ImTextureID>(reinterpret_cast<uintptr>(image->Srv()));
                    draw->AddImage(handle, at, ImVec2{at.x + dimensions.x, at.y + dimensions.y});
                }
                else
                {
                    draw->AddText(ImVec2{low.x + 4.0f, low.y + thumb * 0.4f},
                                   ImGui::GetColorU32(ImGuiCol_TextDisabled), "表示不可");
                }
                const ImVec4 clip{start.x, start.y + thumb + 4.0f, start.x + width, start.y + height};
                draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{clip.x, clip.y},
                               ImGui::GetColorU32(ImGuiCol_Text), name.c_str(), nullptr, width, &clip);
            }
            const char* dragType = nullptr;
            if (extension == ".mat")
            {
                dragType = k_MaterialDragType;
            }
            else if (extension == ".gltf" || extension == ".glb")
            {
                dragType = k_MeshDragType;
            }
            if (dragType != nullptr && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(dragType, file.c_str(), file.size() + 1);
                ImGui::TextUnformatted(name.c_str());
                ImGui::EndDragDropSource();
            }
            if (hovered)
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(name.c_str());
                ImGui::TextUnformatted(file.c_str());
                ImGui::TextUnformatted("クリックでインスペクターに表示");
                if (dragType != nullptr)
                {
                    ImGui::TextUnformatted("Sceneへドラッグして配置・適用");
                }
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }

        void AssetsPanel::RenderFiles(const std::vector<std::string>& files, LevelEditorController& editor)
        {
            std::vector<std::string> previewFiles;
            for (const std::string& file : files)
            {
                const std::string extension = OS::FileSystem::Extension(file);
                if (extension == ".mat" || extension == ".gltf" || extension == ".glb" || IsTextureAssetPath(file))
                {
                    previewFiles.push_back(file);
                }
                else if (extension == ".actor")
                {
                    // 絵を持たないので行で出す。選ぶとインスペクタで欄を直せる
                    if (ImGui::Selectable(OS::FileSystem::FileName(file).c_str(), editor.IsAssetSelected(file)))
                    {
                        editor.SelectAsset(file);
                    }
                }
                else
                {
                    ImGui::TextDisabled("%s", OS::FileSystem::FileName(file).c_str());
                }
            }
            if (previewFiles.empty())
            {
                return;
            }
            const int columns = std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x / 112.0f), 1, 64);
            if (ImGui::BeginTable("##assetTiles", columns,
                                   ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                for (const std::string& file : previewFiles)
                {
                    ImGui::TableNextColumn();
                    RenderTile(file, editor, std::max(1.0f, ImGui::GetContentRegionAvail().x));
                }
                ImGui::EndTable();
            }
        }
#endif

        void AssetsPanel::RenderTree(std::string_view dir, LevelEditorController& editor) noexcept
        {
#if NS_EDITOR_ENABLED
            // 開いた時だけ中身を走査する
            for (const std::string& sub : OS::FileSystem::ListDirectories(dir))
            {
                const std::string label = OS::FileSystem::FileName(sub);
                if (ImGui::TreeNode(label.c_str()))
                {
                    RenderTree(sub, editor);
                    ImGui::TreePop();
                }
            }

            RenderFiles(OS::FileSystem::ListFiles(dir), editor);
#else
            (void)dir;
            (void)editor;
#endif
        }
    } // namespace Editor
} // namespace NS
