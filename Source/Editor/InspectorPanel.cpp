#include "Editor/InspectorPanel.h"
#include "NSlib/CoreObject/Asset/Effect.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "Editor/AssetPicker.h"
#include "Editor/AssetPreview.h"
#include "Editor/EditorActors.h"
#include "Editor/EditorUi.h"
#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Editor/MaterialParamFields.h"
#include "Editor/PanelIds.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/CameraData.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/CoreObject/ClassObjs/HitSensorsObj.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DRenderTarget.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Gfx/Render/BuiltinShaders.h"
#include "NSlib/Gfx/Render/DrawItem.h"
#include "NSlib/Gfx/Render/Skybox.h"

#include "NSlib/Core/Types.h"
#include <algorithm>
#include <cstdio>
#include <format>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <variant>
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
                DrawCheckerboard(*ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), display, 16);
                const ImTextureID handle = static_cast<ImTextureID>(reinterpret_cast<uintptr>(image->Srv()));
                ImGui::Image(handle, display);
            }

            bool DrawDerivedActorNameRow(char* buffer, std::size_t bufferSize, const char* buttonLabel) noexcept
            {
                ImGui::SetNextItemWidth(-ImGui::CalcTextSize(buttonLabel).x - ImGui::GetStyle().ItemSpacing.x * 3.0f);
                ImGui::InputTextWithHint("##derivedName", "派生の名前", buffer, bufferSize);
                ImGui::SameLine();
                return ImGui::Button(buttonLabel);
            }

            // 名前 label の 0 以上のフレーム数の欄を、欄の表の 1 行で描く。直し終えたフレームに true
            // 表で描くのは、同じまとまりの反射の欄と名前の列と値の幅を揃えるため
            bool DrawFrameCount(const char* label, const char* id, int& frames, bool& changed)
            {
                if (!BeginFieldTable(id))
                {
                    return false;
                }
                FieldRow(label);
                changed |= ImGui::DragInt(
                    "##frames", &frames, 0.2f, 0, std::numeric_limits<int>::max(), "%d", ImGuiSliderFlags_AlwaysClamp);
                const bool committed = ImGui::IsItemDeactivatedAfterEdit();
                (void)OverrideButton(false);
                EndFieldTable();
                return committed;
            }

            // material のシェーダーの MaterialParams のうち、読まれている項目の欄を描く
            // 値は SetParamValue がその場で D3DMaterial へ送るので、球のプレビューとシーンは次の描画から変わる
            ValueEditResult DrawMaterialParams(Material& material)
            {
                ValueEditResult result;
                const Gfx::ShaderParamLayout& layout = material.ParamLayout();
                // 読まれない項目は変えても見た目が変わらないので出さない
                const bool anyUsed = std::ranges::any_of(layout.params, &Gfx::ShaderParam::used);
                if (!anyUsed)
                {
                    return result;
                }
                float minimumWidth = 0.0f;
                for (const Gfx::ShaderParam& param : layout.params)
                {
                    if (!param.used)
                    {
                        continue;
                    }
                    const int count = static_cast<int>(material.ParamValue(param).numbers.size());
                    if (count > 0)
                    {
                        minimumWidth = std::max(minimumWidth, MinimumAxisFieldsWidth(count, k_MaterialFloatFormat));
                    }
                }
                ImGui::SeparatorText("シェーダーの値");
                if (!BeginFieldTable("##materialParams", minimumWidth))
                {
                    return result;
                }
                for (const Gfx::ShaderParam& param : layout.params)
                {
                    if (!param.used)
                    {
                        continue;
                    }
                    ImGui::PushID(param.name.c_str());
                    FieldRow(param.name.c_str());
                    MaterialParamValue value = material.ParamValue(param);
                    bool changed = false;
                    switch (param.type)
                    {
                    case Gfx::ShaderParamType::Float:
                    case Gfx::ShaderParamType::Float2:
                    case Gfx::ShaderParamType::Float3:
                    case Gfx::ShaderParamType::Float4:
                        changed = DragParamFloats(value);
                        break;
                    case Gfx::ShaderParamType::Int:
                        changed = DragParamInteger(
                            std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max(), value);
                        break;
                    case Gfx::ShaderParamType::Uint:
                        changed = DragParamInteger(0, std::numeric_limits<uint32>::max(), value);
                        break;
                    case Gfx::ShaderParamType::Bool:
                        changed = DrawParamBool(value);
                        break;
                    case Gfx::ShaderParamType::Unsupported:
                        ImGui::TextDisabled("配列・構造体・行列は欄で触れない");
                        ImGui::PopID();
                        continue;
                    }
                    result.activated |= ImGui::IsItemActivated();
                    result.committed |= ImGui::IsItemDeactivated();
                    (void)OverrideButton(false);
                    // 非数や範囲の外の値は SetParamValue が受けないので、前の値が残る
                    if (changed && material.SetParamValue(param, value))
                    {
                        result.changed = true;
                    }
                    ImGui::PopID();
                }
                EndFieldTable();
                return result;
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
                    commands.SetRenderTarget(m_environmentPreview->Color()->Rtv(),
                                             m_environmentPreview->Depth()->Dsv());
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
                ImGui::TextWrapped("空の画像を読み込めません。プロジェクト内の DDS キューブマップか、六方向の PNG "
                                   "が同じ寸法・形式で揃うフォルダを選んでください。");
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
            if (m_musicSource != editor.SceneMusic().name || m_musicVolumeSource != editor.SceneMusicVolume())
            {
                m_musicSource = editor.SceneMusic().name;
                m_musicVolumeSource = editor.SceneMusicVolume();
                m_musicDraft = m_musicSource;
                m_musicVolumeDraft = m_musicVolumeSource;
            }
            const bool playing = !editor.IsSceneEditable();
            ImGui::BeginDisabled(playing);
            SceneEnvironment fields{
                .skybox = m_skyboxDraft, .music = SoundName{m_musicDraft}, .musicVolume = m_musicVolumeDraft};
            const ValueEditResult edit = DrawReflectedValue(fields);
            if (edit.changed)
            {
                if (fields.skybox != m_skyboxDraft)
                {
                    m_skyboxDraft = fields.skybox;
                    m_skyboxPreviewPath.clear();
                }
                m_musicDraft = fields.music.name;
                m_musicVolumeDraft = ClampMusicVolume(fields.musicVolume);
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
            DrawSceneMusicButtons(editor, playing);
            if (playing)
            {
                ImGui::TextDisabled("背景と曲の変更は編集モードで行います");
            }
        }

        void InspectorPanel::DrawSceneMusicButtons(LevelEditorController& editor, bool playing)
        {
            const bool changed =
                m_musicDraft != editor.SceneMusic().name || m_musicVolumeDraft != editor.SceneMusicVolume();
            ImGui::BeginDisabled(playing || !changed);
            if (ImGui::Button("曲を適用"))
            {
                if (editor.SetSceneMusic(SoundName{m_musicDraft}, m_musicVolumeDraft))
                {
                    m_musicSource = editor.SceneMusic().name;
                    m_musicVolumeSource = editor.SceneMusicVolume();
                    m_musicDraft = m_musicSource;
                    m_musicVolumeDraft = m_musicVolumeSource;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("曲の入力を戻す"))
            {
                m_musicDraft = editor.SceneMusic().name;
                m_musicVolumeDraft = editor.SceneMusicVolume();
            }
            ImGui::EndDisabled();
            if (changed)
            {
                ImGui::TextDisabled("入力した曲と音量は「曲を適用」でシーンに反映します");
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

        void InspectorPanel::DrawHitReactionSets(LevelEditorController& editor, const SceneActor* actor)
        {
            NS_ASSERT(App, actor != nullptr, "束の欄を描くアクターが無い");
            // 束は元の .actor だけが持つので、派生を指すアクターでも元の .actor を読んで書く
            const std::string actorFileName = ActorFileToWrite(actor, FieldDestination::ActorFile);
            const nlohmann::json* document = ActorFileLibrary::Get().Find(actorFileName);
            if (document == nullptr)
            {
                return;
            }
            nlohmann::json source = nlohmann::json::array();
            if (const nlohmann::json* list = ActorFileHitReactions(*document))
            {
                source = *list;
            }
            // 取り消しやほかの窓で文書が変わったら読み直す。直している途中は打った値を残す
            if (m_hitSetsActorFile != actorFileName || (!m_hitSetsEditing && m_hitSetsSource != source))
            {
                m_hitSetsActorFile = actorFileName;
                m_hitSetsSource = std::move(source);
                m_hitSets = ReadHitReactionSets(actorFileName, *document);
                m_hitSetsEditing = false;
            }
            if (m_hitSets.empty() || !ImGui::CollapsingHeader("当たりの束 (.actor)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }
            ImGui::TextDisabled("直し終えた束は元の .actor に書き、元と派生を指すアクターの全部の次の当たりに効く");
            bool committed = false;
            ImGui::PushID("##hitReactions");
            for (int s = 0; s < static_cast<int>(m_hitSets.size()); ++s)
            {
                HitReactionSet& set = m_hitSets[s];
                ImGui::PushID(s);
                // 識別の並びを束の番号だけにするため、開いても木の識別を積まない
                if (ImGui::TreeNodeEx("##set",
                                      ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen,
                                      "%s",
                                      set.name.c_str()))
                {
                    ImGui::Indent();
                    committed |= DrawFrameCount("止めのフレーム数", "##stopFrames", set.stopFrames, m_hitSetsEditing);
                    for (int e = 0; e < static_cast<int>(set.entries.size()); ++e)
                    {
                        HitReactionEntry& entry = set.entries[e];
                        ImGui::PushID(e);
                        // 項目は畳める見出しにし、閉じていても長さが読めるようにする
                        const std::string label{HitReactionItemLabel(entry.item)};
                        if (ImGui::TreeNodeEx("##entry",
                                              ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_NoTreePushOnOpen,
                                              "%s (%d フレーム)",
                                              label.c_str(),
                                              entry.length))
                        {
                            ImGui::Indent();
                            committed |= DrawFrameCount("長さ", "##length", entry.length, m_hitSetsEditing);
                            if (const ReflectionInfo* info = HitReactionItemReflection(entry.item))
                            {
                                const ValueEditResult fields =
                                    DrawReflectedValue(HitReactionItemFields(entry.item), *info);
                                m_hitSetsEditing |= fields.changed;
                                committed |= fields.committed;
                            }
                            // 値の表は欄の表に載らないので、名前ごとに数の欄を出す。名前の足し引きはしない
                            HitOwnerItem* owner = std::get_if<HitOwnerItem>(&entry.item);
                            if (owner != nullptr && !owner->values.empty() && BeginFieldTable("##ownerValues"))
                            {
                                for (std::pair<const std::string, float>& ownerValue : owner->values)
                                {
                                    const std::string& name = ownerValue.first;
                                    float& number = ownerValue.second;
                                    ImGui::PushID(name.c_str());
                                    FieldRow(name.c_str());
                                    m_hitSetsEditing |= ImGui::DragFloat("##ownerValue", &number, 0.01f);
                                    committed |= ImGui::IsItemDeactivatedAfterEdit();
                                    (void)OverrideButton(false);
                                    ImGui::PopID();
                                }
                                EndFieldTable();
                            }
                            ImGui::Unindent();
                        }
                        ImGui::PopID();
                    }
                    ImGui::Unindent();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
            if (committed)
            {
                editor.WriteActorFileHitReactions(m_hitSetsActorFile, m_hitSets);
                m_hitSetsEditing = false;
            }
        }

        void InspectorPanel::DrawHitSensors(LevelEditorController& editor, SceneActor* actor)
        {
            NS_ASSERT(App, actor != nullptr, "センサーの欄を描くアクターが無い");
            HitSensorsObj* sensors = actor->HitSensorsClassObj();
            if (sensors == nullptr)
            {
                return;
            }
            const std::vector<HitSensor*>& list = sensors->Sensors();
            const bool hasValues = std::any_of(list.begin(), list.end(), [](const HitSensor* sensor) {
                const ReflectionInfo* info = sensor->GetReflection();
                return info != nullptr && info->fieldCount > 0;
            });
            if (!hasValues || !ImGui::CollapsingHeader("センサー (.actor)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }
            ImGui::TextDisabled("直し終えた値は元の .actor に書き、元と派生を指すアクターの全部のセンサーに効く");
            bool committed = false;
            ImGui::PushID("##hitSensors");
            for (HitSensor* sensor : list)
            {
                const ReflectionInfo* info = sensor->GetReflection();
                if (info == nullptr || info->fieldCount == 0)
                {
                    continue;
                }
                ImGui::PushID(sensor->Name().c_str());
                if (ImGui::TreeNodeEx("##sensor",
                                      ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen,
                                      "%s",
                                      sensor->Name().c_str()))
                {
                    ImGui::Indent();
                    // 値はこのアクターのセンサーへすぐ効かせ、確定の時に .actor へ書いてほかのアクターへ広げる
                    const ClassObjEditResult edit = DrawReflectedClassObj(sensor);
                    committed |= edit.committed;
                    ImGui::Unindent();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
            if (committed)
            {
                // センサーの値は束と同じく元の .actor だけが持つ
                editor.WriteActorFileHitSensors(ActorFileToWrite(actor, FieldDestination::ActorFile), *sensors);
            }
        }

        void InspectorPanel::DrawSelectedAsset(LevelEditorController& editor, Object& asset)
        {
            const auto duplicate = [&](const std::string& path) {
                ImGui::InputText("複製名", m_effectCloneName, sizeof(m_effectCloneName));
                if (!ImGui::Button("複製"))
                {
                    return false;
                }
                editor.CommitAssetEdit();
                const std::optional<std::string> copy =
                    AssetManager::Get().DuplicateEffectFile(path, m_effectCloneName);
                if (!copy)
                {
                    NS_LOG_WARN(App, "複製できない。名前と未保存の変更を確認してください");
                    return false;
                }
                m_effectCloneName[0] = '\0';
                editor.SelectAsset(OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), *copy));
                return true;
            };

            if (const Effect* effect = Cast<Effect>(&asset))
            {
                ImGui::TextUnformatted(effect->Label().c_str());
                ImGui::TextWrapped("%s", effect->Path().c_str());
                (void)duplicate(effect->Path());
                return;
            }
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
                const ValueEditResult paramEdit = DrawMaterialParams(*material);
                ImGui::EndDisabled();
                if (edit.changed || paramEdit.changed)
                {
                    editor.MarkAssetEdited();
                }
                if (edit.committed || paramEdit.committed)
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
            if (SceneActor* actorFile = Cast<SceneActor>(&asset))
            {
                // .actor の欄は、NS_GLOBAL_FIELD の欄も含めて全部 .actor へ書く。直し終えた時にファイルへ書き、同じ
                // .actor のアクターへ写す
                ImGui::Text("%s.actor (%s)", actorFile->ActorFileName().c_str(), actorFile->ClassName());
                ImGui::TextDisabled("ここで直した値は、この .actor を指すアクターの全部に効く");
                // 作れない訳はエディタが WARN で出す。作れた時だけ名前の欄を空にする
                if (DrawDerivedActorNameRow(m_derivedNameBuffer, sizeof(m_derivedNameBuffer), "派生を作る") &&
                    editor.CreateDerivedActorFile(actorFile->ActorFileName(), m_derivedNameBuffer))
                {
                    m_derivedNameBuffer[0] = '\0';
                }
                ImGui::Separator();
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##actorFileFieldFilter", "欄の検索", m_fieldFilter, sizeof(m_fieldFilter));
                bool committed = false;
                ForEachReflectedClassObj(actorFile, [&](const ClassObjInfo& row, Object* part) {
                    if (part->GetReflection() == nullptr)
                    {
                        return;
                    }

                    ImGui::PushID(row.key);
                    if (ImGui::CollapsingHeader(row.key, ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        const ClassObjEditResult edit = DrawReflectedClassObj(part, {}, nullptr, m_fieldFilter);
                        if (edit.changedTarget != nullptr && edit.changedField != nullptr)
                        {
                            editor.FinishFieldWrite(actorFile, edit.changedField->type);
                            editor.NoteActorFileFieldEdit(row, edit.changedField->name);
                        }
                        committed |= edit.committed;
                    }
                    ImGui::PopID();
                });
                DrawHitSensors(editor, actorFile);
                DrawHitReactionSets(editor, actorFile);
                // 確定で .actor の文書が変わると、この 1 体は次に引いた時に作り直される
                if (committed)
                {
                    editor.CommitAssetEdit();
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

        std::vector<ActorRefOption> MakeActorRefOptions(const ActorList& objects)
        {
            const std::vector<SceneActor*> placed = PlacedActors(objects);
            std::vector<ActorRefOption> options;
            options.reserve(placed.size());
            for (std::size_t i = 0; i < placed.size(); ++i)
            {
                const SceneActor* candidate = placed[i];
                char label[96];
                std::snprintf(
                    label, sizeof(label), "[%zu] %s (id %u)", i, ActorDisplayName(candidate), candidate->Id());
                options.push_back(ActorRefOption{candidate->Id(), label});
            }
            return options;
        }

        void InspectorPanel::DrawActorFileChoice(const SceneActor* actor)
        {
            NS_ASSERT(App, actor != nullptr, "DrawActorFileChoice: 渡したアクターか部品が無い");
#if NS_EDITOR_ENABLED
            ImGui::TextUnformatted((actor->ActorFileName() + ".actor").c_str());
            if (DrawDerivedActorNameRow(m_derivedNameBuffer, sizeof(m_derivedNameBuffer), "上書きから派生を作る"))
            {
                m_deriveRequested = true;
                m_actorFileTargetId = actor->Id();
            }
#else
            (void)actor;
#endif
        }

        void InspectorPanel::Render(LevelEditorController& editor) noexcept
        {
#if NS_EDITOR_ENABLED
            m_nameCommitId = 0;
            m_deriveRequested = false;
            const ImGuiStyle& style = ImGui::GetStyle();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{style.ItemSpacing.x, style.ItemSpacing.y + 3.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{style.CellPadding.x, style.CellPadding.y + 2.0f});
            if (ImGui::Begin(k_PanelInspector))
            {
                if (ImGui::SmallButton("シーン設定"))
                {
                    editor.SelectSceneSettings();
                }
                const std::vector<ActorRefOption> refOptions = MakeActorRefOptions(editor.Actors());

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
                    if (ImGui::CollapsingHeader("エディタの見た目", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        (void)DrawReflectedValue(m_appearance);
                    }
                    ImGui::End();
                    ImGui::PopStyleVar(2);
                    return;
                }

                // 直前の HasInspectableSelection で同じ識別番号の対象を確認済み
                SceneActor* go = editor.SelectedActor();

                // 名前の正は実体。一覧での改名と Undo も映すよう、打っていない間は毎回入れ直す
                const uint32 selectedId = editor.SelectedActorId();
                // 打っている間は入れ直さない。打った名前は打ち始めた対象へ確定する
                if (m_nameEditId == 0)
                {
                    m_nameId = selectedId;
                    std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "%s", Editor::ActorDisplayName(go));
                }
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##actorName", m_nameBuffer, sizeof(m_nameBuffer));
                if (ImGui::IsItemActivated())
                {
                    m_nameEditId = m_nameId;
                }
                // 改名はアクターを組み直すので、このパネルを描き終えてから流す
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
                DrawActorFileChoice(go);

                ImGui::Separator();

                // 部品はクラスが決めるので、ここでは値だけを変える。足し引きはできない

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##fieldFilter", "欄の検索", m_fieldFilter, sizeof(m_fieldFilter));

                Editor::ClassObjEditResult classObjEdit{};
                // 戻すは描いた後に行うので、対象の部品の行を控える
                const ClassObjInfo* revertRow = nullptr;
                ForEachReflectedClassObj(go, [&](const ClassObjInfo& field, Object* live) {
                    if (live->GetReflection() == nullptr)
                    {
                        return;
                    }
                    const std::string typeName{live->ClassName()};

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
                        const Editor::ClassObjEditResult r = Editor::DrawReflectedClassObj(
                            live, refOptions, field.key, m_fieldFilter, go, &m_appearance);
                        classObjEdit.activated |= r.activated;
                        classObjEdit.committed |= r.committed;
                        classObjEdit.changed |= r.changed;
                        if (r.revertField != nullptr)
                        {
                            classObjEdit.revertTarget = r.revertTarget;
                            classObjEdit.revertField = r.revertField;
                            revertRow = &field;
                        }
                        // 書く先は欄の種類で決まる。.actor へ書く欄は確定の時に書くので、直した欄を伝えておく
                        if (r.changedTarget != nullptr && r.changedField != nullptr)
                        {
                            editor.FinishFieldWrite(go, r.changedField->type);
                            editor.NoteFieldEdit(go, field, r.changedField->name);
                        }
                    }
                    ImGui::PopID();
                });
                DrawHitSensors(editor, go);
                DrawHitReactionSets(editor, go);
                // 戻すは控えを取ってから実体を書く。順を逆にすると変更後が控えになり履歴が空になる
                if (classObjEdit.revertTarget != nullptr && revertRow != nullptr)
                {
                    editor.BeginClassObjEdit();
                    // 上書きの一覧から外し、.actor の全部の値の同じ項目を写す
                    if (RevertSceneOverride(go, *revertRow, classObjEdit.revertField->name))
                    {
                        editor.FinishFieldWrite(go, classObjEdit.revertField->type);
                        // 既定へ戻すのも手編集。プレイ中は凍結からも項目を消す
                        editor.MirrorPlayEditToBaseline(go, *revertRow, classObjEdit.revertField->name);
                    }
                    editor.CommitClassObjEdit();
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
                editor.RenameActor(m_nameCommitId, m_nameBuffer);
            }
            // 作れない訳はエディタが WARN で出す。作れた時だけ名前の欄を空にする
            if (m_deriveRequested && editor.DeriveActorFileFromActor(m_actorFileTargetId, m_derivedNameBuffer))
            {
                m_derivedNameBuffer[0] = '\0';
            }
#else
            (void)editor;
#endif
        }
    } // namespace Editor
} // namespace NS
