#include "Editor/InspectorPanel.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "Editor/EditorObjects.h"
#include "Editor/AssetPicker.h"
#include "Editor/AssetPreview.h"
#include "Editor/EditorUi.h"
#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/CameraData.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/D3DRenderTarget.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Gfx/Render/DrawItem.h"
#include "NSlib/Gfx/Render/BuiltinShaders.h"
#include "NSlib/Gfx/Render/Skybox.h"
#include "NSlib/Windows/Filesystem.h"

#include "NSlib/Core/Types.h"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS
{
    namespace Editor
    {
        InspectorPanel::InspectorPanel() = default;
        InspectorPanel::~InspectorPanel() = default;

#if NS_EDITOR_ENABLED
        namespace
        {
            struct EnvironmentFields
            {
                std::string skybox;

                NS_REFLECT_BEGIN(EnvironmentFields, void)
                NS_FIELD(skybox, "skybox", "スカイボックス")
                NS_REFLECT_END_VALUE()
            };

            // 置いたばかりの配置物の姿。ここから動かした欄だけ印を出す
            const Vector3 k_DefaultPosition{0.0f, 0.0f, 0.0f};
            const Vector3 k_DefaultScale{1.0f, 1.0f, 1.0f};

            void DrawTexturePreview(const Texture& texture)
            {
                const Gfx::D3DTexture* image = texture.D3D();
                if (!image->IsValid() || image->IsUsingFallback())
                {
                    ImGui::TextDisabled("画像を読み込めません");
                    return;
                }
                const Size2D size = image->Size();
                ImGui::Text("%d × %d px", size.width, size.height);
                const float available = std::max(1.0f, std::min(256.0f, ImGui::GetContentRegionAvail().x));
                const float scale = available / static_cast<float>(std::max(size.width, size.height));
                const ImVec2 display{size.width * scale, size.height * scale};
                const ImVec2 start = ImGui::GetCursorScreenPos();
                ImDrawList* draw = ImGui::GetWindowDrawList();
                for (int y = 0; y < static_cast<int>(display.y); y += 16)
                {
                    for (int x = 0; x < static_cast<int>(display.x); x += 16)
                    {
                        ImU32 color = IM_COL32(55, 55, 55, 255);
                        if ((x / 16 + y / 16) % 2 == 0)
                        {
                            color = IM_COL32(80, 80, 80, 255);
                        }
                        draw->AddRectFilled(ImVec2{start.x + x, start.y + y},
                                            ImVec2{start.x + std::min(x + 16.0f, display.x),
                                                   start.y + std::min(y + 16.0f, display.y)}, color);
                    }
                }
                const ImTextureID handle = static_cast<ImTextureID>(reinterpret_cast<uintptr>(image->Srv()));
                ImGui::Image(handle, display);
            }
        } // namespace

        bool InspectorPanel::DrawSkyboxPreview(std::string_view path)
        {
            if (path.empty())
            {
                ImGui::TextDisabled("スカイボックスなし");
                return true;
            }
            Application* app = Application::Get();
            if (app == nullptr)
            {
                return false;
            }
            if (!m_environmentPreview)
            {
                m_environmentPreview = Gfx::D3DRenderTarget::Create(Size2D{320, 180});
            }
            if (!m_environmentPreview->IsValid())
            {
                ImGui::TextDisabled("プレビューを作成できません");
                return false;
            }
            // 同じ参照先の読み込み失敗は繰り返さず、「再読み込み」で再試行する。
            if (m_skyboxPreviewPath != path)
            {
                m_skyboxPreviewPath = path;
                m_skyboxPreviewFailed = true;
                const std::optional<std::string> resolved =
                    OS::FileSystem::ResolveUnder(OS::FileSystem::ContentRoot(), path);
                if (resolved && IsSkyboxAssetPath(*resolved))
                {
                    m_skyboxPreview = std::make_unique<Gfx::Skybox>(AssetManager::Get().LoadBuiltinShaders());
                    CameraData camera;
                    camera.SetPosition(Vector3::Zero);
                    camera.SetTarget(Vector3{0.0f, 0.0f, 1.0f});
                    Gfx::D3DDevice& device = app->Device();
                    Gfx::D3DCommandList& commands = device.Commands();
                    commands.SetRenderTarget(m_environmentPreview->Color()->Rtv(), m_environmentPreview->Depth()->Dsv());
                    commands.SetViewport(320.0f, 180.0f);
                    commands.ClearRenderTarget(m_environmentPreview->Color()->Rtv(), 0.12f, 0.12f, 0.14f, 1.0f);
                    commands.ClearDepth(m_environmentPreview->Depth()->Dsv());
                    m_skyboxPreview->Draw(device, camera, path);
                    device.BindBackbuffer();
                    m_skyboxPreviewFailed = !m_skyboxPreview->IsValid() || m_skyboxPreview->IsUsingFallback();
                }
            }
            if (m_skyboxPreviewFailed)
            {
                ImGui::TextWrapped("空の画像を読み込めません。プロジェクト内の DDS キューブマップか、六方向の PNG が同じ寸法・形式で揃うフォルダを選んでください。");
                return false;
            }
            const float width = std::max(1.0f, std::min(320.0f, ImGui::GetContentRegionAvail().x));
            const ImTextureID image =
                static_cast<ImTextureID>(reinterpret_cast<uintptr>(m_environmentPreview->Color()->Srv()));
            ImGui::Image(image, ImVec2{width, width * 180.0f / 320.0f});
            ImGui::TextDisabled("空のプレビュー（正面）");
            return true;
        }

        void InspectorPanel::DrawSceneSettings(LevelEditorController& editor)
        {
            ImGui::SeparatorText("シーン設定");
            if (!ImGui::CollapsingHeader("環境", ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }
            if (m_skyboxSource != editor.SkyboxPath())
            {
                m_skyboxSource = editor.SkyboxPath();
                m_skyboxDraft = m_skyboxSource;
                m_skyboxPreviewPath.clear();
            }
            const bool playing = !editor.IsSceneEditable();
            ImGui::BeginDisabled(playing);
            EnvironmentFields fields{m_skyboxDraft};
            const ValueEditResult edit = DrawReflectedValue(fields);
            if (edit.changed)
            {
                m_skyboxDraft = fields.skybox;
                m_skyboxPreviewPath.clear();
            }
            static std::vector<AssetOption> options;
            if (ImGui::Button("画像を選択..."))
            {
                options = CollectSkyboxOptions(OS::FileSystem::ContentRoot());
                ImGui::OpenPopup("##skybox-picker");
            }
            if (ImGui::BeginPopup("##skybox-picker"))
            {
                ImGui::TextUnformatted("スカイボックスを選択");
                ImGui::Separator();
                for (const AssetOption& option : options)
                {
                    if (ImGui::Selectable(option.value.c_str(), option.value == m_skyboxDraft))
                    {
                        m_skyboxDraft = option.value;
                        m_skyboxPreviewPath.clear();
                    }
                }
                if (options.empty())
                {
                    ImGui::TextDisabled("対応する画像が Assets 内にありません");
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("再読み込み"))
            {
                m_skyboxPreviewPath.clear();
            }
            ImGui::TextDisabled("参照先はプロジェクトからの相対パス");
            ImGui::EndDisabled();

            const bool ready = DrawSkyboxPreview(m_skyboxDraft);
            const bool changed = m_skyboxDraft != editor.SkyboxPath();
            ImGui::BeginDisabled(playing || !ready || !changed);
            if (ImGui::Button("適用"))
            {
                if (editor.SetSkyboxPath(m_skyboxDraft))
                {
                    m_skyboxDraft = editor.SkyboxPath();
                    m_skyboxSource = m_skyboxDraft;
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(playing || editor.SkyboxPath().empty());
            if (ImGui::Button("表示しない"))
            {
                (void)editor.SetSkyboxPath("");
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(playing || !changed);
            if (ImGui::Button("入力を戻す"))
            {
                m_skyboxDraft = editor.SkyboxPath();
                m_skyboxPreviewPath.clear();
            }
            ImGui::EndDisabled();
            if (changed)
            {
                ImGui::TextDisabled("入力した参照先は「適用」でシーンに反映します");
            }
            if (playing)
            {
                ImGui::TextDisabled("背景の変更は編集モードで行います");
            }
        }

        void InspectorPanel::DrawAssetPreview(const Gfx::DrawItem& item)
        {
            Application* app = Application::Get();
            if (app == nullptr || !app->IsValid())
            {
                ImGui::TextDisabled("描画装置がないためプレビューできません");
                return;
            }
            if (!m_assetPreview)
            {
                m_assetPreview = Gfx::D3DRenderTarget::Create({256, 256});
            }
            if (!RenderAssetPreview(app->Device(), *m_assetPreview, item))
            {
                ImGui::TextDisabled("素材を描画できません");
                return;
            }

            const float width = std::max(1.0f, std::min(256.0f, ImGui::GetContentRegionAvail().x));
            const ImTextureID handle =
                static_cast<ImTextureID>(reinterpret_cast<uintptr>(m_assetPreview->UiTextureHandle()));
            const ImVec2 start = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##assetPreview", ImVec2{width, width});
            ImGui::GetWindowDrawList()->AddImage(handle, start, ImVec2{start.x + width, start.y + width});
        }

        void InspectorPanel::DrawMaterialPreview(const Material& material)
        {
            const std::optional<Gfx::DrawItem> item = MakeAssetPreviewItem(material);
            if (!item)
            {
                ImGui::TextDisabled("このマテリアルのプレビューには対応していません");
                return;
            }
            DrawAssetPreview(*item);
        }

        void InspectorPanel::DrawMeshPreview(const Mesh& mesh)
        {
            if (m_previewMeshPath != mesh.Path())
            {
                m_previewMeshPath = mesh.Path();
                m_previewYaw = 0.45f;
                m_previewPitch = -0.15f;
                m_previewZoom = 1.0f;
            }

            const std::optional<Gfx::DrawItem> item =
                MakeAssetPreviewItem(mesh, AssetPreviewPose{m_previewYaw, m_previewPitch, m_previewZoom});
            if (!item)
            {
                ImGui::TextDisabled("モデルを描画できません");
                return;
            }
            DrawAssetPreview(*item);
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
            const ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                m_previewYaw += io.MouseDelta.x * 0.01f;
                m_previewPitch = std::clamp(m_previewPitch + io.MouseDelta.y * 0.01f, -1.4f, 1.4f);
            }
            if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
            {
                m_previewZoom = std::clamp(m_previewZoom + io.MouseWheel * 0.1f, 0.4f, 2.5f);
            }
            ImGui::TextDisabled("形状プレビュー：左ドラッグで回転 / ホイールで拡大");
            if (ImGui::Button("視点を戻す"))
            {
                m_previewYaw = 0.45f;
                m_previewPitch = -0.15f;
                m_previewZoom = 1.0f;
            }
        }

        void InspectorPanel::DrawSelectedAsset(LevelEditorController& editor, Object& asset)
        {
            if (Material* material = Cast<Material>(&asset))
            {
                ImGui::TextUnformatted(material->Path().c_str());
                DrawMaterialPreview(*material);
                // 共有のマテリアルと、テクスチャが欄の枠より多い .mat は書けないので見るだけにする
                const bool writable = AssetManager::Get().MakeMaterialFileJson(*material).has_value();
                if (!writable)
                {
                    ImGui::TextDisabled("見るだけ (.mat に書けないマテリアル)");
                }
                ImGui::Separator();
                ImGui::BeginDisabled(!writable);
                const ValueEditResult edit = DrawReflectedValue(material, *material->GetReflection());
                ImGui::EndDisabled();
                if (edit.changed)
                {
                    editor.MarkAssetEdited();
                }
                if (edit.committed)
                {
                    editor.CommitAssetEdit();
                }
                ImGui::SeparatorText("使用テクスチャ");
                std::size_t slot = 0;
                for (const std::string& path : material->File().textures)
                {
                    ++slot;
                    if (path.empty())
                    {
                        continue;
                    }
                    ImGui::Text("テクスチャ %zu", slot);
                    ImGui::TextWrapped("%s", path.c_str());
                    const std::string resolved = OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), path);
                    if (OS::FileSystem::Exists(resolved))
                    {
                        DrawTexturePreview(*AssetManager::Get().LoadTexture(resolved));
                    }
                    else
                    {
                        ImGui::TextDisabled("画像ファイルが見つかりません");
                    }
                }
                return;
            }
            if (const Texture* texture = Cast<Texture>(&asset))
            {
                ImGui::TextWrapped("%s", texture->Path().c_str());
                ImGui::Separator();
                DrawTexturePreview(*texture);
                return;
            }
            if (const Mesh* mesh = Cast<Mesh>(&asset))
            {
                ImGui::TextUnformatted(mesh->Path().c_str());
                ImGui::Separator();
                DrawMeshPreview(*mesh);
                if (const AABB* bounds = mesh->LocalBounds())
                {
                    ImGui::Text("幅 %.3f  高さ %.3f  奥行き %.3f",
                                bounds->Extents.x * 2.0f,
                                bounds->Extents.y * 2.0f,
                                bounds->Extents.z * 2.0f);
                }
            }
        }
#endif

        std::vector<ObjectRefOption> MakeObjectRefOptions(const ActorList& objects)
        {
            const std::vector<SceneActor*> placed = PlacedActors(objects);
            std::vector<ObjectRefOption> options;
            options.reserve(placed.size());
            for (std::size_t i = 0; i < placed.size(); ++i)
            {
                const SceneActor& candidate = *placed[i];
                char label[96];
                std::snprintf(label, sizeof(label), "[%zu] %s (id %u)", i, ObjectDisplayName(candidate), candidate.Id());
                options.push_back(ObjectRefOption{candidate.Id(), label});
            }
            return options;
        }

        void InspectorPanel::Render(LevelEditorController& editor) noexcept
        {
#if NS_EDITOR_ENABLED
            m_nameCommitId = 0;
            const ImGuiStyle& style = ImGui::GetStyle();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                ImVec2{style.ItemSpacing.x, style.ItemSpacing.y + 3.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                                ImVec2{style.CellPadding.x, style.CellPadding.y + 2.0f});
            if (ImGui::Begin(k_PanelInspector))
            {
                if (ImGui::SmallButton("シーン設定"))
                {
                    editor.SelectSceneSettings();
                }
                const std::vector<ObjectRefOption> refOptions = MakeObjectRefOptions(editor.Objects());

                // 素材の選択はアクターの選択と排他なので、選んでいる間はアクターの欄を出さない
                if (Object* asset = editor.SelectedAsset())
                {
                    DrawSelectedAsset(editor, *asset);
                    ImGui::End();
                    ImGui::PopStyleVar(2);
                    return;
                }

                if (!editor.HasInspectableSelection())
                {
                    DrawSceneSettings(editor);
                    ImGui::End();
                    ImGui::PopStyleVar(2);
                    return;
                }

                // 直前の HasInspectableSelection で同じ識別番号の対象を確認済み
                SceneActor* go = editor.SelectedObjectActor();

                // 名前の正は実体。一覧での改名と Undo も映すよう、打っていない間は毎回入れ直す
                const uint32 selectedId = editor.SelectedObjectId();
                // 打っている間は入れ直さない。打った名前は打ち始めた対象へ確定する
                if (m_nameEditId == 0)
                {
                    m_nameId = selectedId;
                    std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "%s", Editor::ObjectDisplayName(*go));
                }
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##objectName", m_nameBuffer, sizeof(m_nameBuffer));
                if (ImGui::IsItemActivated())
                {
                    m_nameEditId = m_nameId;
                }
                // 改名は配置物を組み直すので、このパネルを描き終えてから流す
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    m_nameCommitId = m_nameEditId;
                }
                if (ImGui::IsItemDeactivated())
                {
                    m_nameEditId = 0;
                }
                // 番号は参照先の候補とアクター一覧と同じ PlacedActors の並びの位置
                for (std::size_t i = 0; i < refOptions.size(); ++i)
                {
                    if (refOptions[i].id == selectedId)
                    {
                        ImGui::Text("[%zu] %s", i, go->ClassName());
                        break;
                    }
                }
                ImGui::Separator();

                // Transform は runtime が唯一の出所なので即反映し、commit が undo へ確定する
                ImGui::SeparatorText("Transform");

                if (ImGui::BeginTable("##transform", 2,
                                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
                {
                    ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed,
                                            ImGui::CalcTextSize("回転 (度)").x);
                    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
                    const auto transformRow = [&](const char* id,
                                                  const char* label,
                                                  float(&values)[3],
                                                  float speed,
                                                  bool moved,
                                                  const std::function<void()>& apply,
                                                  const std::function<void()>& revert) {
                        ImGui::PushID(id);
                        Editor::FieldRow(label);
                        const char* axes[] = {"X", "Y", "Z"};
                        const ImVec4 colors[] = {{1.0f, 0.45f, 0.45f, 1.0f},
                                                 {0.5f, 0.9f, 0.5f, 1.0f},
                                                 {0.45f, 0.7f, 1.0f, 1.0f}};
                        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
                        const float axisWidth = ImGui::CalcTextSize("X").x;
                        const float inputWidth =
                            (ImGui::CalcItemWidth() - axisWidth * 3.0f - spacing * 5.0f) / 3.0f;
                        ImGui::BeginGroup();
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            if (axis > 0)
                            {
                                ImGui::SameLine(0.0f, spacing);
                            }
                            ImGui::PushID(axes[axis]);
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextColored(colors[axis], "%s", axes[axis]);
                            ImGui::SameLine(0.0f, spacing);
                            ImGui::SetNextItemWidth(std::max(inputWidth, 1.0f));
                            const bool changed = ImGui::DragFloat("##value", &values[axis], speed);
                            if (ImGui::IsItemActivated())
                            {
                                editor.BeginTransformEdit();
                            }
                            if (changed)
                            {
                                apply();
                            }
                            if (ImGui::IsItemDeactivated())
                            {
                                editor.CommitTransformEdit();
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndGroup();
                        if (Editor::RevertButton(moved))
                        {
                            editor.BeginTransformEdit();
                            revert();
                            editor.CommitTransformEdit();
                        }
                        ImGui::PopID();
                    };

                    const Vector3 posVec = go->Root().Position();
                    float pos[3] = {posVec.x, posVec.y, posVec.z};
                    transformRow(
                        "position",
                        "位置",
                        pos,
                        0.05f,
                        posVec != k_DefaultPosition,
                        [&] { editor.SetSelectedFreePosition(Vector3{pos[0], pos[1], pos[2]}); },
                        [&] { editor.SetSelectedFreePosition(k_DefaultPosition); });

                    // 回転は内部のクォータニオンを度のオイラー角へ変換して編集し、入力をクォータニオンへ戻す
                    // 滑らかに回し続けるならギズモの回転ツールが向く。ここは角度の直接入力 / 微調整用
                    const Quaternion q = go->Root().Rotation();
                    const Vector3 euler = QuaternionToEulerDegrees(q);
                    float rot[3] = {euler.x, euler.y, euler.z};
                    transformRow(
                        "rotation",
                        "回転 (度)",
                        rot,
                        0.5f,
                        q != Quaternion::Identity,
                        [&] {
                            const Vector3 degrees{rot[0], rot[1], rot[2]};
                            editor.SetSelectedFreeRotation(EulerDegreesToQuaternion(degrees));
                        },
                        [&] { editor.SetSelectedFreeRotation(Quaternion::Identity); });

                    const Vector3 sclVec = go->Root().Scale();
                    float scl[3] = {sclVec.x, sclVec.y, sclVec.z};
                    transformRow(
                        "scale",
                        "スケール",
                        scl,
                        0.05f,
                        sclVec != k_DefaultScale,
                        [&] { editor.SetSelectedFreeScale(Vector3{scl[0], scl[1], scl[2]}); },
                        [&] { editor.SetSelectedFreeScale(k_DefaultScale); });

                    Editor::EndFieldTable();
                }

                ImGui::Separator();

                // 部品はクラスが決めるので、ここでは値だけを変える。足し引きはできない

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##fieldFilter", "欄の検索", m_fieldFilter, sizeof(m_fieldFilter));

                Editor::ClassObjEditResult classObjEdit{};
                // 戻すと種類の既定にするは描いた後に行うので、対象の部品の行を控える
                const ClassObjInfo* revertRow = nullptr;
                const ClassObjInfo* promoteRow = nullptr;
                ForEachReflectedClassObj(*go, [&](const ClassObjInfo& field, Object* live) {
                    if (live->GetReflection() == nullptr)
                    {
                        return;
                    }
                    const std::string typeName{live->ClassName()};
                    // 根の部品は上の専用パネルが編集するので一覧に出さない
                    if (live == &go->Root())
                    {
                        return;
                    }

                    ImGui::PushID(field.key);

                    ImGui::PushStyleColor(ImGuiCol_Header, Editor::k_ClassObjHeaderColor);
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Editor::k_ClassObjHeaderHoveredColor);
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, Editor::k_ClassObjHeaderActiveColor);
                    std::string header{field.key};
                    if (header != typeName)
                    {
                        header += " (";
                        header += typeName;
                        header += ")";
                    }
                    header += "###component";
                    const bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                    ImGui::PopStyleColor(3);

                    if (open)
                    {
                        // 比べる相手は持ち主のクラスの既定の部品。種類の既定値まで当たっている
                        const Object* baseline = FindBaselineClassObj(*go, field);
                        const Editor::ClassObjEditResult r =
                            Editor::DrawReflectedClassObj(*live, refOptions, baseline, m_fieldFilter, go, &field);
                        classObjEdit.activated |= r.activated;
                        classObjEdit.committed |= r.committed;
                        classObjEdit.changed |= r.changed;
                        if (r.revertField != nullptr)
                        {
                            classObjEdit.revertTarget = r.revertTarget;
                            classObjEdit.revertField = r.revertField;
                            revertRow = &field;
                        }
                        if (r.promoteField != nullptr)
                        {
                            classObjEdit.promoteTarget = r.promoteTarget;
                            classObjEdit.promoteField = r.promoteField;
                            promoteRow = &field;
                        }
                        // プレイ中の手編集は編集復帰の組み直しで消えるので、編集された欄だけ凍結側へも写す
                        if (r.changedTarget != nullptr && r.changedField != nullptr)
                        {
                            editor.FinishFieldWrite(*go, r.changedField->type);
                            editor.MirrorPlayEditToBaseline(*go, field, r.changedField->name);
                        }
                    }
                    ImGui::PopID();
                });
                // 戻すは控えを取ってから実体を書く。順を逆にすると変更後が控えになり履歴が空になる
                if (classObjEdit.revertTarget != nullptr && revertRow != nullptr)
                {
                    const Object* baseline = FindBaselineClassObj(*go, *revertRow);
                    if (baseline != nullptr)
                    {
                        editor.BeginClassObjEdit();
                        Editor::RevertFieldToDefault(*classObjEdit.revertTarget, *baseline, *classObjEdit.revertField);
                        editor.FinishFieldWrite(*go, classObjEdit.revertField->type);
                        // 既定へ戻すのも手編集。プレイ中は凍結側へも写して残す
                        editor.MirrorPlayEditToBaseline(*go, *revertRow, classObjEdit.revertField->name);
                        editor.CommitClassObjEdit();
                    }
                }
                // 種類の既定にするのは、同じ種類の全ての個体とファイルを書き換える。既定の部品を作り直すので描画の後で行う
                if (classObjEdit.promoteField != nullptr && promoteRow != nullptr)
                {
                    (void)editor.PromoteFieldToArchetype(*go, *promoteRow, classObjEdit.promoteField->name);
                }
                if (classObjEdit.activated)
                {
                    editor.BeginClassObjEdit();
                }
                if (classObjEdit.committed)
                {
                    editor.CommitClassObjEdit();
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(2);

            if (m_nameCommitId != 0)
            {
                editor.RenameObject(m_nameCommitId, m_nameBuffer);
            }
#else
            (void)editor;
#endif
        }
    } // namespace Editor
} // namespace NS
