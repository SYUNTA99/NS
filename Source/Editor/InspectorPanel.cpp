#include "Editor/InspectorPanel.h"

#include "Editor/EditorObjects.h"
#include "Editor/EditorUi.h"
#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Object/World.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
#if NS_EDITOR_ENABLED
    namespace
    {
        // 置いたばかりの配置物の姿。 ここから動かした欄だけ印を出す
        const NS::Math::Vector3 k_DefaultPosition{0.0f, 0.0f, 0.0f};
        const NS::Math::Vector3 k_DefaultScale{1.0f, 1.0f, 1.0f};

        // data の component 1 件に対応する live を永続 id で引く
        // live は priority 順、data は書かれた順で並びが揃わないため、位置でなく id で名指しする
        NS::Object::Component* FindLiveComponentById(NS::Object::GameObject& go, std::uint32_t id) noexcept
        {
            if (id == 0)
                return nullptr;
            for (NS::Object::Component* comp : go.Components())
            {
                if (comp != nullptr && comp->Id() == id)
                    return comp;
            }
            return nullptr;
        }
    } // namespace
#endif

    void InspectorPanel::Render(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        m_nameCommitId = 0;
        if (ImGui::Begin(k_PanelInspector))
        {
            // ObjectRef フィールドの参照先候補。 Hierarchy と同じ並びと表示名で全配置物を出す
            std::vector<NS::Editor::ObjectRefOption> refOptions;
            refOptions.reserve(editor.World().ObjectCount());
            for (std::size_t i = 0; i < editor.World().ObjectCount(); ++i)
            {
                NS::Object::GameObject& candidate = *editor.World().ObjectAt(i);
                if (candidate.IsTransient())
                    continue;
                char label[96];
                std::snprintf(label,
                              sizeof(label),
                              "[%zu] %s (id %u)",
                              i,
                              NS::Editor::ObjectDisplayName(candidate),
                              candidate.Id());
                refOptions.push_back(NS::Editor::ObjectRefOption{candidate.Id(), label});
            }

            if (editor.IsCameraSelected())
            {
                ImGui::Text("カメラ");
                ImGui::Separator();
                // ブレンド秒の Brain と編集中=free-fly / プレイ中=follow の現在 active な vcam を反射で出す
                // 値はライブで効き保存はしない
                auto* brain = editor.CameraBrainObject();
                auto* vcam = editor.ActiveVirtualCameraObject();
                if (brain == nullptr && vcam == nullptr)
                    ImGui::TextDisabled("(カメラなし)");
                if (brain != nullptr)
                    (void)NS::Editor::DrawObjectComponents(*brain, refOptions);
                if (vcam != nullptr && vcam != brain)
                    (void)NS::Editor::DrawObjectComponents(*vcam, refOptions);

                ImGui::End();
                return;
            }

            if (!editor.HasInspectableSelection())
            {
                ImGui::TextDisabled("(選択なし)");
                ImGui::End();
                return;
            }

            const NS::Object::ObjectData obj = editor.SelectedObjectSnapshot();

            // 名前は直接ここで書き換えられる。 選択が変わったら今の表示名を入れ直す
            const std::uint32_t selectedId = editor.SelectedObjectId();
            if (m_nameId != selectedId)
            {
                m_nameId = selectedId;
                std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "%s", NS::Editor::ObjectDisplayName(obj));
            }
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##objectName", m_nameBuffer, sizeof(m_nameBuffer));
            // 改名は world を組み直すので、 このパネルを描き終えてから流す
            if (ImGui::IsItemDeactivatedAfterEdit())
                m_nameCommitId = selectedId;
            ImGui::Text("[%zu] %s", editor.SelectedObjectIndex(), obj.className.c_str());
            ImGui::Separator();

            // Transform は runtime が真実の源なので即反映し、 commit が undo へ確定する
            ImGui::SeparatorText("Transform");

            if (NS::Editor::BeginFieldTable("##transform"))
            {
                const NS::Math::Vector3 posVec = NS::Object::ObjectPosition(obj);
                float pos[3] = {posVec.x, posVec.y, posVec.z};
                const bool posMoved = (posVec != k_DefaultPosition);
                ImGui::PushID("position");
                NS::Editor::FieldRow("位置");
                if (ImGui::DragFloat3("##value", pos, 0.05f))
                    editor.SetSelectedFreePosition(NS::Math::Vector3{pos[0], pos[1], pos[2]});
                if (ImGui::IsItemActivated())
                    editor.BeginTransformEdit();
                if (ImGui::IsItemDeactivatedAfterEdit())
                    editor.CommitTransformEdit();
                if (NS::Editor::RevertButton(posMoved))
                {
                    editor.BeginTransformEdit();
                    editor.SetSelectedFreePosition(k_DefaultPosition);
                    editor.CommitTransformEdit();
                }
                ImGui::PopID();

                // 回転は内部 quaternion を度の Euler に直して編集し、 入力を quaternion へ戻す
                // 滑らかに回し続けるならギズモ R が向く。 ここは角度の直接入力 / 微調整用
                const NS::Math::Quaternion q = NS::Object::ObjectRotation(obj);
                const NS::Math::Vector3 euler = q.ToEuler();
                float rot[3] = {NS::Math::RadiansToDegrees(euler.x),
                                NS::Math::RadiansToDegrees(euler.y),
                                NS::Math::RadiansToDegrees(euler.z)};
                const bool turned = (q != NS::Math::Quaternion::Identity);
                ImGui::PushID("rotation");
                NS::Editor::FieldRow("回転");
                if (ImGui::DragFloat3("##value", rot, 0.5f))
                    editor.SetSelectedFreeRotation(NS::Math::Quaternion::CreateFromYawPitchRoll(
                        NS::Math::Vector3{NS::Math::DegreesToRadians(rot[0]),
                                          NS::Math::DegreesToRadians(rot[1]),
                                          NS::Math::DegreesToRadians(rot[2])}));
                if (ImGui::IsItemActivated())
                    editor.BeginTransformEdit();
                if (ImGui::IsItemDeactivatedAfterEdit())
                    editor.CommitTransformEdit();
                if (NS::Editor::RevertButton(turned))
                {
                    editor.BeginTransformEdit();
                    editor.SetSelectedFreeRotation(NS::Math::Quaternion::Identity);
                    editor.CommitTransformEdit();
                }
                ImGui::PopID();

                const NS::Math::Vector3 sclVec = NS::Object::ObjectScale(obj);
                float scl[3] = {sclVec.x, sclVec.y, sclVec.z};
                const bool resized = (sclVec != k_DefaultScale);
                ImGui::PushID("scale");
                NS::Editor::FieldRow("スケール");
                if (ImGui::DragFloat3("##value", scl, 0.05f))
                    editor.SetSelectedFreeScale(NS::Math::Vector3{scl[0], scl[1], scl[2]});
                if (ImGui::IsItemActivated())
                    editor.BeginTransformEdit();
                if (ImGui::IsItemDeactivatedAfterEdit())
                    editor.CommitTransformEdit();
                if (NS::Editor::RevertButton(resized))
                {
                    editor.BeginTransformEdit();
                    editor.SetSelectedFreeScale(k_DefaultScale);
                    editor.CommitTransformEdit();
                }
                ImGui::PopID();

                NS::Editor::EndFieldTable();
            }

            // MeshRendererComponent の Material フィールドは反射一覧に出る。 適用は Assets パネルのドロップ /
            // クリックから
            ImGui::Separator();

            if (obj.components.empty())
                ImGui::TextDisabled("コンポーネント無し。 足すと表示・当たりが付く");

            // 反射編集は live component へ直接入る。 live は priority 順なので data の並びへ型と出現番号で対応づける
            NS::Object::GameObject* go = editor.SelectedObjectGameObject();
            NS::Editor::ComponentEditResult componentEdit{};
            for (std::size_t k = 0; k < obj.components.size(); ++k)
            {
                const std::string typeName{NS::Object::ComponentEntryType(obj.components[k])};
                // Transform は上の専用パネルが編集するので一覧に出さない
                if (typeName == "TransformComponent")
                    continue;

                ImGui::PushID(static_cast<int>(k));

                // プレイヤーの印の入力 component は下ろすと player でなくなるので札を触らせない
                const bool lockedComponent = (typeName == "PlayerInputComponent");
                bool enabled = NS::Object::ComponentEntryEnabled(obj.components[k]);
                ImGui::BeginDisabled(lockedComponent);
                if (ImGui::Checkbox("##enabled", &enabled))
                    editor.SetComponentEnabledOnSelected(k, enabled);
                ImGui::EndDisabled();
                ImGui::SameLine();

                // ヘッダを中身より明るくして、 どこからどこまでが 1 個か見えるようにする
                ImGui::PushStyleColor(ImGuiCol_Header, NS::Editor::k_ComponentHeaderColor);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, NS::Editor::k_ComponentHeaderHoveredColor);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, NS::Editor::k_ComponentHeaderActiveColor);
                // AllowOverlap 無しだとヘッダが全幅の当たりを取り、 右端に重ねた「...」がクリックを拾えない
                const bool open = ImGui::CollapsingHeader(
                    typeName.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
                ImGui::PopStyleColor(3);

                // コピー / 削除はヘッダ右端の三点メニューへまとめる
                const float menuWidth = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                ImGui::SameLine(ImGui::GetContentRegionMax().x - menuWidth);
                if (ImGui::SmallButton("..."))
                    ImGui::OpenPopup("ComponentMenu");
                if (ImGui::BeginPopup("ComponentMenu"))
                {
                    if (ImGui::MenuItem("コンポーネントをコピー"))
                        editor.CopyComponentToClipboard(k);
                    // 最後の 1 個は消すと空構成になる。 プレイヤーの印の入力 component も消させない
                    const bool canRemove = obj.components.size() > 1 && typeName != "PlayerInputComponent";
                    if (ImGui::MenuItem("コンポーネントを削除", nullptr, false, canRemove))
                        editor.RemoveComponentFromSelected(k);
                    ImGui::EndPopup();
                }

                if (open)
                {
                    NS::Object::Component* live = nullptr;
                    if (go != nullptr)
                        live = FindLiveComponentById(*go, NS::Object::ComponentEntryId(obj.components[k]));

                    if (live != nullptr && live->GetReflection() != nullptr)
                    {
                        const NS::Object::Component* baseline = m_defaults.Find(typeName);
                        const NS::Editor::ComponentEditResult r =
                            NS::Editor::DrawReflectedComponent(*live, refOptions, baseline);
                        componentEdit.activated |= r.activated;
                        componentEdit.committed |= r.committed;
                        componentEdit.changed |= r.changed;
                        if (r.revertField != nullptr)
                        {
                            componentEdit.revertTarget = r.revertTarget;
                            componentEdit.revertField = r.revertField;
                        }
                    }
                    else
                        ImGui::TextDisabled("調整できるパラメータなし");
                }
                ImGui::PopID();
            }
            // 戻すは控えを取ってから live を書く。 順を逆にすると変更後が控えになり履歴が空になる
            if (componentEdit.revertTarget != nullptr && componentEdit.revertField != nullptr)
            {
                const NS::Object::Component* baseline = m_defaults.Find(componentEdit.revertTarget->ClassName());
                if (baseline != nullptr)
                {
                    editor.BeginComponentEdit();
                    NS::Editor::RevertFieldToDefault(
                        *componentEdit.revertTarget, *baseline, *componentEdit.revertField);
                    editor.CommitComponentEdit();
                }
            }
            if (componentEdit.activated)
                editor.BeginComponentEdit();
            if (componentEdit.committed)
                editor.CommitComponentEdit();

            if (editor.HasClipboardComponent())
            {
                if (ImGui::Button("コンポーネントを貼り付け"))
                    editor.PasteClipboardComponentToSelected();
            }

            // Add Component は一番下に置く
            ImGui::Separator();
            if (ImGui::Button("+ コンポーネントを追加"))
            {
                m_addComponentFilter[0] = '\0'; // 開くたびに検索欄を空へ戻す
                ImGui::OpenPopup("AddComponentPopup");
            }
            if (ImGui::BeginPopup("AddComponentPopup"))
            {
                // 開いた最初のフレームだけ検索欄へフォーカスを当てる
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint(
                    "##addComponentFilter", "検索", m_addComponentFilter, sizeof(m_addComponentFilter));
                ImGui::Separator();
                for (const std::string& name : NS::Object::RegisteredNames())
                {
                    // プレイヤーの印である入力 component は手で足させない
                    if (name == "PlayerInputComponent")
                        continue;
                    if (!NameMatches(name.c_str(), m_addComponentFilter))
                        continue;
                    if (ImGui::Selectable(name.c_str()))
                        editor.AddComponentToSelected(name);
                }
                ImGui::EndPopup();
            }
        }
        ImGui::End();

        if (m_nameCommitId != 0)
            editor.RenameObject(m_nameCommitId, m_nameBuffer);
#else
        (void)editor;
#endif
    }
} // namespace NS::Editor
