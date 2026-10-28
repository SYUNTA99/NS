#include "Editor/AssetsPanel.h"

#include "Editor/AssetPicker.h"
#include "Editor/AssetPreview.h"
#include "Editor/DragTypes.h"
#include "Editor/EditorUi.h"
#include "Editor/LevelEditorController.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/PanelIds.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/D3DRenderTarget.h"
#include "NSlib/Gfx/D3DTexture.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS
{
    namespace Editor
    {
        struct AssetsPanel::ActorThumbnail
        {
            nlohmann::json source;
            uint64 editVersion = 0;
            bool rendered = false;
            std::unique_ptr<Gfx::D3DRenderTarget> target;
        };

        AssetsPanel::AssetsPanel() = default;
        AssetsPanel::~AssetsPanel() = default;

        void AssetsPanel::Render(LevelEditorController& editor) noexcept
        {
#if NS_EDITOR_ENABLED
            if (ImGui::Begin(k_PanelAssets))
            {
                // 適用にはギズモ選択が要るので枠は Object モード時だけ出す
                if (editor.GizmoToolActive())
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

                const std::string assetsDirectory = OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), "Assets");
                RenderTree(assetsDirectory, editor);
                m_revealSceneFolder = false;
                RenderCreateMenu(assetsDirectory, editor, false);
                RenderSceneCreate(editor);
                RenderSceneOpen(editor);
            }
            ImGui::End();
#else
            (void)editor;
#endif
        }

#if NS_EDITOR_ENABLED
        void AssetsPanel::RenderCreateMenu(std::string_view directory, LevelEditorController& editor, bool onFolder)
        {
            bool open = false;
            if (onFolder)
            {
                open = ImGui::BeginPopupContextItem(std::string{directory}.c_str());
            }
            else
            {
                open = ImGui::BeginPopupContextWindow(
                    "##createSceneInAssets", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems);
            }
            if (!open)
            {
                return;
            }
            if (ImGui::MenuItem("シーンを作成...", nullptr, false, editor.IsSceneEditable()))
            {
                const std::optional<std::string> relative = OS::FileSystem::RelativeUnder(
                    OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), "Assets"), directory);
                m_sceneCreateDirectory = "Scenes";
                if (relative && !relative->empty())
                {
                    m_sceneCreateDirectory = *relative;
                }
                m_newSceneName = "New Scene";
                m_sceneCreateError.clear();
                m_sceneCreateRequested = true;
            }
            ImGui::EndPopup();
        }

        void AssetsPanel::RenderSceneCreate(LevelEditorController& editor)
        {
            if (m_sceneCreateRequested)
            {
                ImGui::OpenPopup("シーンを作成");
                m_sceneCreateRequested = false;
            }
            if (!ImGui::BeginPopupModal("シーンを作成", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                return;
            }
            ImGui::Text("保存先: Assets/%s", m_sceneCreateDirectory.c_str());
            if (ImGui::IsWindowAppearing())
            {
                ImGui::SetKeyboardFocusHere();
            }
            m_newSceneName.resize(std::max(m_newSceneName.size() + 1, std::size_t{256}));
            const bool submitted = ImGui::InputText(
                "名前", m_newSceneName.data(), m_newSceneName.size(), ImGuiInputTextFlags_EnterReturnsTrue);
            m_newSceneName.resize(std::strlen(m_newSceneName.c_str()));
            ImGui::BeginDisabled(!editor.IsSceneEditable());
            // 名前の欄で Enter を押した時は、押せないボタンを通らずに来る。編集できるかも CreateLevel が答える
            if (ImGui::Button("作成") || submitted)
            {
                // 作れるかの確かめは CreateLevel が持ち、ここは答えの理由ごとに文を出すだけ
                const std::string name = m_sceneCreateDirectory + "/" + m_newSceneName;
                switch (editor.Editor().CreateLevel(name))
                {
                case CreateLevelResult::Created:
                    m_sceneCreateDirectory = OS::FileSystem::ParentDirectory(name);
                    m_revealSceneFolder = true;
                    ImGui::CloseCurrentPopup();
                    break;
                case CreateLevelResult::BadPath:
                    m_sceneCreateError = "名前と保存先のフォルダには、英数字、空白、_、- を使ってください。";
                    break;
                case CreateLevelResult::AlreadyExists:
                    m_sceneCreateError = "同じ名前のシーンが既にあります。";
                    break;
                case CreateLevelResult::NotEditable:
                    m_sceneCreateError = "プレイ中はシーンを作成できません。";
                    break;
                case CreateLevelResult::WriteFailed:
                    m_sceneCreateError = "シーンを作成できませんでした。保存先を確認してください。";
                    break;
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("キャンセル"))
            {
                ImGui::CloseCurrentPopup();
            }
            if (!m_sceneCreateError.empty())
            {
                ImGui::TextWrapped("%s", m_sceneCreateError.c_str());
            }
            ImGui::EndPopup();
        }

        bool AssetsPanel::OpenPendingScene(LevelEditorController& editor)
        {
            if (!editor.Editor().OpenLevel(m_pendingSceneName))
            {
                m_sceneOpenError = "シーンを読み込めませんでした: " + m_pendingSceneName;
                m_pendingSceneName.clear();
                return false;
            }
            m_pendingSceneName.clear();
            m_sceneOpenError.clear();
            return true;
        }

        void AssetsPanel::RenderSceneOpen(LevelEditorController& editor)
        {
            bool opened = false;
            if (!m_pendingSceneName.empty() && editor.IsSceneEditable() && !ImGui::IsPopupOpen("シーンの切り替え"))
            {
                // 入力欄で直している途中の値は、まだ履歴に入っていない。先に確定して、未保存かを問う。
                // 確かめの窓を開いた後は、窓が答えを受けるまでここを通らない
                editor.CommitTransformEdit();
                editor.CommitClassObjEdit();
                if (editor.Editor().HasUnsavedChanges())
                {
                    ImGui::OpenPopup("シーンの切り替え");
                }
                else
                {
                    opened = OpenPendingScene(editor);
                }
            }
            if (ImGui::BeginPopupModal("シーンの切り替え", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextUnformatted("未保存の変更があります。変更を破棄してシーンを開きますか？");
                ImGui::TextUnformatted(m_pendingSceneName.c_str());
                if (ImGui::Button("破棄して開く"))
                {
                    opened = OpenPendingScene(editor);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("キャンセル"))
                {
                    m_pendingSceneName.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (opened)
            {
                ImGui::SetWindowFocus(k_PanelScene);
            }
            if (!m_sceneOpenError.empty())
            {
                ImGui::TextWrapped("%s", m_sceneOpenError.c_str());
            }
        }

        const Gfx::D3DTexture* AssetsPanel::LoadActorThumbnail(const std::string& file, LevelEditorController& editor)
        {
            const std::string name = OS::FileSystem::Stem(file);
            const nlohmann::json* values = ActorFileLibrary::Get().Values(name);
            if (values == nullptr)
            {
                return nullptr;
            }
            std::unique_ptr<ActorThumbnail>& cached = m_actorThumbnails[file];
            if (!cached)
            {
                cached = std::make_unique<ActorThumbnail>();
            }
            const uint64 version = editor.Editor().Undo().Version();
            if (cached->source == *values && cached->editVersion == version)
            {
                if (cached->rendered)
                {
                    return cached->target->Color();
                }
                return nullptr;
            }
            cached->source = *values;
            cached->editVersion = version;
            cached->rendered = false;
            const std::unique_ptr<SceneActor> actor =
                CreateActorOfClass(ActorFileLibrary::Get().ClassNameOf(name), ActorInitInfo{.actorFile = name});
            if (!actor || actor->ModelClassObj() == nullptr)
            {
                return nullptr;
            }
            if (Animation* animation = actor->AnimationClassObj())
            {
                animation->BindToModel(actor->ModelClassObj());
            }
            const std::optional<Gfx::DrawItem> item = MakeAssetPreviewItem(*actor);
            if (!item)
            {
                return nullptr;
            }
            if (!cached->target)
            {
                cached->target = Gfx::D3DRenderTarget::Create({80, 80});
            }
            cached->rendered = RenderAssetPreview(Application::Get()->Device(), *cached->target, *item);
            if (!cached->rendered)
            {
                return nullptr;
            }
            return cached->target->Color();
        }

        const Gfx::D3DTexture* AssetsPanel::LoadThumbnail(const std::string& file, LevelEditorController& editor)
        {
            if (IsEffectAssetPath(file) || IsEffekseerAssetPath(file))
            {
                return nullptr;
            }
            Application* app = Application::Get();
            if (app == nullptr || !app->IsValid())
            {
                return nullptr;
            }
            if (IsActorFileAssetPath(file))
            {
                return LoadActorThumbnail(file, editor);
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
            const std::optional<std::string> name = OS::FileSystem::RelativeUnder(OS::FileSystem::ContentRoot(), file);
            if (!name)
            {
                NS_LOG_ERROR(App, "素材 {} が ContentRoot の外にあるので下見を作らない", file);
                return nullptr;
            }
            Object* asset = nullptr;
            if (IsMaterialAssetPath(file))
            {
                asset = assets.LoadMaterial(*name);
            }
            else
            {
                asset = assets.LoadMesh(*name);
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
            const float thumb = std::min(80.0f, width);
            const float height = thumb + ImGui::GetTextLineHeight() * 2.0f + 10.0f;
            ImGui::PushID(file.c_str());
            const ImVec2 start = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable(
                    "##asset", editor.IsAssetSelected(file), ImGuiSelectableFlags_None, ImVec2{width, height}))
            {
                editor.SelectAsset(file);
            }
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemVisible())
            {
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const ImVec2 low{start.x + (width - thumb) * 0.5f, start.y};
                DrawCheckerboard(*draw, low, ImVec2{thumb, thumb}, 8);
                if (const Gfx::D3DTexture* image = LoadThumbnail(file, editor))
                {
                    const Size2D size = image->Size();
                    const float scale = thumb / static_cast<float>(std::max(size.width, size.height));
                    const ImVec2 dimensions{size.width * scale, size.height * scale};
                    const ImVec2 at{low.x + (thumb - dimensions.x) * 0.5f, low.y + (thumb - dimensions.y) * 0.5f};
                    const ImTextureID handle = static_cast<ImTextureID>(reinterpret_cast<uintptr>(image->Srv()));
                    draw->AddImage(handle, at, ImVec2{at.x + dimensions.x, at.y + dimensions.y});
                }
                else
                {
                    draw->AddText(ImVec2{low.x + 4.0f, low.y + thumb * 0.4f},
                                  ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                  "表示不可");
                }
                const ImVec4 clip{start.x, start.y + thumb + 4.0f, start.x + width, start.y + height};
                draw->AddText(ImGui::GetFont(),
                              ImGui::GetFontSize(),
                              ImVec2{clip.x, clip.y},
                              ImGui::GetColorU32(ImGuiCol_Text),
                              name.c_str(),
                              nullptr,
                              width,
                              &clip);
            }
            const char* dragType = nullptr;
            if (IsMaterialAssetPath(file))
            {
                dragType = k_MaterialDragType;
            }
            else if (IsModelAssetPath(file))
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
                const std::optional<std::string> relative =
                    OS::FileSystem::RelativeUnder(OS::FileSystem::ContentRoot(), file);
                std::string sceneName;
                if (relative)
                {
                    sceneName = LevelNameOfScenePath(*relative);
                }
                if (!sceneName.empty() && sceneName.find('/') != std::string::npos)
                {
                    ImGui::BeginDisabled(!editor.IsSceneEditable());
                    const bool selected = editor.Editor().CurrentLevelName() == sceneName;
                    if (ImGui::Selectable(OS::FileSystem::FileName(file).c_str(), selected))
                    {
                        m_sceneOpenError.clear();
                        m_pendingSceneName = sceneName;
                    }
                    ImGui::EndDisabled();
                }
                else if (IsMaterialAssetPath(file) || IsModelAssetPath(file) || IsTextureAssetPath(file) ||
                         IsActorFileAssetPath(file))
                {
                    previewFiles.push_back(file);
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
            if (ImGui::BeginTable(
                    "##assetTiles", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
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
                const std::optional<std::string> relative = OS::FileSystem::RelativeUnder(
                    OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), "Assets"), sub);
                if (m_revealSceneFolder && relative &&
                    (*relative == m_sceneCreateDirectory || m_sceneCreateDirectory.starts_with(*relative + "/")))
                {
                    ImGui::SetNextItemOpen(true);
                }
                const bool open = ImGui::TreeNode(label.c_str());
                RenderCreateMenu(sub, editor, true);
                if (open)
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
