#include "Editor/InspectorPanel.h"

#include "Editor/EditorObjects.h"
#include "Editor/EditorUi.h"
#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/SceneData.h"

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
        // 置いたばかりの配置物の姿。ここから動かした欄だけ印を出す
        const NS::Core::Vector3 k_DefaultPosition{0.0f, 0.0f, 0.0f};
        const NS::Core::Vector3 k_DefaultScale{1.0f, 1.0f, 1.0f};

        // 添字は CaptureObjectData の並びへそのまま渡る。絞り方を変えると編集操作が隣を掴む
        std::vector<NS::Object::Component*> ReflectedComponents(NS::Object::GameObject& go)
        {
            std::vector<NS::Object::Component*> result;
            result.reserve(go.Components().size());
            for (NS::Object::Component* comp : go.Components())
            {
                if (comp == nullptr || comp->GetReflection() == nullptr)
                {
                    continue;
                }
                result.push_back(comp);
            }
            return result;
        }
    } // namespace
#endif

    void InspectorPanel::Render(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        m_nameCommitId = 0;
        if (ImGui::Begin(k_PanelInspector))
        {
            // ObjectRef フィールドの参照先候補。Hierarchy と同じ並びと表示名で全配置物を出す
            std::vector<NS::Editor::ObjectRefOption> refOptions;
            refOptions.reserve(editor.Objects().ObjectCount());
            for (std::size_t i = 0; i < editor.Objects().ObjectCount(); ++i)
            {
                NS::Object::GameObject& candidate = *editor.Objects().ObjectAt(i);
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

            if (!editor.HasInspectableSelection())
            {
                ImGui::TextDisabled("(選択なし)");
                ImGui::End();
                return;
            }

            // 直前の HasInspectableSelection が同じ id を引けているので非 null
            NS::Object::GameObject* go = editor.SelectedObjectGameObject();
            const std::vector<NS::Object::Component*> components = ReflectedComponents(*go);

            // 名前は直接ここで書き換えられる。選択が変わったら今の表示名を入れ直す
            const std::uint32_t selectedId = editor.SelectedObjectId();
            if (m_nameId != selectedId)
            {
                m_nameId = selectedId;
                std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "%s", NS::Editor::ObjectDisplayName(*go));
            }
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##objectName", m_nameBuffer, sizeof(m_nameBuffer));
            // 改名は配置物を組み直すので、このパネルを描き終えてから流す
            if (ImGui::IsItemDeactivatedAfterEdit())
                m_nameCommitId = selectedId;
            ImGui::Text("[%zu] %s", editor.SelectedObjectIndex(), go->ClassName());
            ImGui::Separator();

            // Transform は runtime が唯一の出所なので即反映し、commit が undo へ確定する
            ImGui::SeparatorText("Transform");

            if (NS::Editor::BeginFieldTable("##transform"))
            {
                const NS::Core::Vector3 posVec = go->Root().Position();
                float pos[3] = {posVec.x, posVec.y, posVec.z};
                const bool posMoved = (posVec != k_DefaultPosition);
                ImGui::PushID("position");
                NS::Editor::FieldRow("位置");
                if (ImGui::DragFloat3("##value", pos, 0.05f))
                    editor.SetSelectedFreePosition(NS::Core::Vector3{pos[0], pos[1], pos[2]});
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

                // 回転は内部 quaternion を度の Euler に直して編集し、入力を quaternion へ戻す
                // 滑らかに回し続けるならギズモの回転ツールが向く。ここは角度の直接入力 / 微調整用
                const NS::Core::Quaternion q = go->Root().Rotation();
                const NS::Core::Vector3 euler = q.ToEuler();
                float rot[3] = {NS::Core::RadiansToDegrees(euler.x),
                                NS::Core::RadiansToDegrees(euler.y),
                                NS::Core::RadiansToDegrees(euler.z)};
                const bool turned = (q != NS::Core::Quaternion::Identity);
                ImGui::PushID("rotation");
                NS::Editor::FieldRow("回転");
                if (ImGui::DragFloat3("##value", rot, 0.5f))
                    editor.SetSelectedFreeRotation(NS::Core::Quaternion::CreateFromYawPitchRoll(
                        NS::Core::Vector3{NS::Core::DegreesToRadians(rot[0]),
                                          NS::Core::DegreesToRadians(rot[1]),
                                          NS::Core::DegreesToRadians(rot[2])}));
                if (ImGui::IsItemActivated())
                    editor.BeginTransformEdit();
                if (ImGui::IsItemDeactivatedAfterEdit())
                    editor.CommitTransformEdit();
                if (NS::Editor::RevertButton(turned))
                {
                    editor.BeginTransformEdit();
                    editor.SetSelectedFreeRotation(NS::Core::Quaternion::Identity);
                    editor.CommitTransformEdit();
                }
                ImGui::PopID();

                const NS::Core::Vector3 sclVec = go->Root().Scale();
                float scl[3] = {sclVec.x, sclVec.y, sclVec.z};
                const bool resized = (sclVec != k_DefaultScale);
                ImGui::PushID("scale");
                NS::Editor::FieldRow("スケール");
                if (ImGui::DragFloat3("##value", scl, 0.05f))
                    editor.SetSelectedFreeScale(NS::Core::Vector3{scl[0], scl[1], scl[2]});
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

            // MeshRendererComponent の Material フィールドはリフレクション一覧に出る。適用は Assets パネルのドロップ /
            // クリックから
            ImGui::Separator();

            if (components.empty())
                ImGui::TextDisabled("コンポーネント無し。 足すと表示・当たりが付く");

            NS::Editor::ComponentEditResult componentEdit{};
            for (std::size_t k = 0; k < components.size(); ++k)
            {
                const std::string typeName{components[k]->ClassName()};
                // Transform は上の専用パネルが編集するので一覧に出さない
                if (typeName == "TransformComponent")
                    continue;

                ImGui::PushID(static_cast<int>(k));

                // 入力 component を休止させると player が動かなくなるので active を触らせない
                const bool lockedComponent = (typeName == "PlayerInputComponent");
                bool enabled = components[k]->IsEnabled();
                ImGui::BeginDisabled(lockedComponent);
                if (ImGui::Checkbox("##enabled", &enabled))
                    editor.SetComponentEnabledOnSelected(k, enabled);
                ImGui::EndDisabled();
                ImGui::SameLine();

                // ヘッダを中身より明るくして、どこからどこまでが 1 個か見えるようにする
                ImGui::PushStyleColor(ImGuiCol_Header, NS::Editor::k_ComponentHeaderColor);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, NS::Editor::k_ComponentHeaderHoveredColor);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, NS::Editor::k_ComponentHeaderActiveColor);
                // AllowOverlap 無しだとヘッダが全幅の当たりを取り、右端に重ねた「...」がクリックを拾えない
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
                    // 最後の 1 個は消すと空構成になる。プレイヤーの印の入力 component も消させない
                    const bool canRemove = components.size() > 1 && typeName != "PlayerInputComponent";
                    if (ImGui::MenuItem("コンポーネントを削除", nullptr, false, canRemove))
                        editor.RemoveComponentFromSelected(k);
                    ImGui::EndPopup();
                }

                if (open)
                {
                    NS::Object::Component* live = components[k];
                    if (live->GetReflection() != nullptr)
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
                        // プレイ中の手編集は編集復帰の組み直しで消えるので、編集された欄だけ凍結側へも写す
                        if (r.changedTarget != nullptr && r.changedField != nullptr)
                            editor.MirrorPlayEditToBaseline(*r.changedTarget, r.changedField->name);
                    }
                    else
                        ImGui::TextDisabled("調整できるパラメータなし");
                }
                ImGui::PopID();
            }
            // 戻すは控えを取ってから live を書く。順を逆にすると変更後が控えになり履歴が空になる
            if (componentEdit.revertTarget != nullptr && componentEdit.revertField != nullptr)
            {
                const NS::Object::Component* baseline = m_defaults.Find(componentEdit.revertTarget->ClassName());
                if (baseline != nullptr)
                {
                    editor.BeginComponentEdit();
                    NS::Editor::RevertFieldToDefault(
                        *componentEdit.revertTarget, *baseline, *componentEdit.revertField);
                    // 既定へ戻すのも手編集。プレイ中は凍結側へも写して残す
                    editor.MirrorPlayEditToBaseline(*componentEdit.revertTarget, componentEdit.revertField->name);
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
