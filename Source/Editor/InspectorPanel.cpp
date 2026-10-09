#include "Editor/InspectorPanel.h"

#include "Editor/EditorObjects.h"
#include "Editor/EditorUi.h"
#include "Editor/InspectorReflection.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Object/ActorList.h"
#include "NSlib/Object/Reflection/SubObjectEntry.h"
#include "NSlib/Object/Scene/SceneJson.h"

#include <cstdint>
#include <cstdio>
#include <functional>
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
        const NS::Vector3 k_DefaultPosition{0.0f, 0.0f, 0.0f};
        const NS::Vector3 k_DefaultScale{1.0f, 1.0f, 1.0f};

        std::vector<NS::Obj::SubObject*> ReflectedSubObjects(NS::Obj::Actor& go)
        {
            std::vector<NS::Obj::SubObject*> result;
            for (NS::Obj::SubObject* subObject : go.SubObjs())
            {
                if (subObject->GetReflection() != nullptr)
                {
                    result.push_back(subObject);
                }
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
            // ActorRef の欄の参照先候補。Hierarchy と同じ並びと表示名で全配置物を出す
            std::vector<NS::Editor::ObjectRefOption> refOptions;
            refOptions.reserve(editor.Objects().ObjectCount());
            for (std::size_t i = 0; i < editor.Objects().ObjectCount(); ++i)
            {
                NS::Obj::Actor& candidate = *editor.Objects().ObjectAt(i);
                if (candidate.IsTransient())
                {
                    continue;
                }
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
            NS::Obj::Actor* go = editor.SelectedObjectActor();
            const std::vector<NS::Obj::SubObject*> subObjects = ReflectedSubObjects(*go);

            // 名前は直接ここで書き換えられる。選択が変わったら今の表示名を入れ直す
            const std::uint32_t selectedId = editor.SelectedObjectId();
            // 打っている間は入れ直さない。打った名前は打ち始めた対象へ確定する
            if (m_nameEditId == 0 && m_nameId != selectedId)
            {
                m_nameId = selectedId;
                std::snprintf(m_nameBuffer, sizeof(m_nameBuffer), "%s", NS::Editor::ObjectDisplayName(*go));
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
            ImGui::Text("[%zu] %s", editor.SelectedObjectIndex(), go->ClassName());
            ImGui::Separator();

            // Transform は runtime が唯一の出所なので即反映し、commit が undo へ確定する
            ImGui::SeparatorText("Transform");

            if (NS::Editor::BeginFieldTable("##transform"))
            {
                const auto transformRow = [&](const char* id,
                                              const char* label,
                                              float(&values)[3],
                                              float speed,
                                              bool moved,
                                              const std::function<void()>& apply,
                                              const std::function<void()>& revert) {
                    ImGui::PushID(id);
                    NS::Editor::FieldRow(label);
                    if (ImGui::DragFloat3("##value", values, speed))
                    {
                        apply();
                    }
                    if (ImGui::IsItemActivated())
                    {
                        editor.BeginTransformEdit();
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        editor.CommitTransformEdit();
                    }
                    if (NS::Editor::RevertButton(moved))
                    {
                        editor.BeginTransformEdit();
                        revert();
                        editor.CommitTransformEdit();
                    }
                    ImGui::PopID();
                };

                const NS::Vector3 posVec = go->Root().Position();
                float pos[3] = {posVec.x, posVec.y, posVec.z};
                transformRow(
                    "position",
                    "位置",
                    pos,
                    0.05f,
                    posVec != k_DefaultPosition,
                    [&] { editor.SetSelectedFreePosition(NS::Vector3{pos[0], pos[1], pos[2]}); },
                    [&] { editor.SetSelectedFreePosition(k_DefaultPosition); });

                // 回転は内部 quaternion を度の Euler に直して編集し、入力を quaternion へ戻す
                // 滑らかに回し続けるならギズモの回転ツールが向く。ここは角度の直接入力 / 微調整用
                const NS::Quaternion q = go->Root().Rotation();
                const NS::Vector3 euler = NS::QuaternionToEulerDegrees(q);
                float rot[3] = {euler.x, euler.y, euler.z};
                transformRow(
                    "rotation",
                    "回転",
                    rot,
                    0.5f,
                    q != NS::Quaternion::Identity,
                    [&] {
                        const NS::Vector3 degrees{rot[0], rot[1], rot[2]};
                        editor.SetSelectedFreeRotation(NS::EulerDegreesToQuaternion(degrees));
                    },
                    [&] { editor.SetSelectedFreeRotation(NS::Quaternion::Identity); });

                const NS::Vector3 sclVec = go->Root().Scale();
                float scl[3] = {sclVec.x, sclVec.y, sclVec.z};
                transformRow(
                    "scale",
                    "スケール",
                    scl,
                    0.05f,
                    sclVec != k_DefaultScale,
                    [&] { editor.SetSelectedFreeScale(NS::Vector3{scl[0], scl[1], scl[2]}); },
                    [&] { editor.SetSelectedFreeScale(k_DefaultScale); });

                NS::Editor::EndFieldTable();
            }

            // Model のマテリアルの欄はリフレクション一覧に出る。適用は Assets パネルのドロップ /
            // クリックから
            ImGui::Separator();

            // 部品はクラスが決めるので、ここでは値だけを変える。足し引きはできない

            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##fieldFilter", "欄の検索", m_fieldFilter, sizeof(m_fieldFilter));

            NS::Editor::SubObjectEditResult subObjectEdit{};
            for (std::size_t k = 0; k < subObjects.size(); ++k)
            {
                const std::string typeName{subObjects[k]->ClassName()};
                // 根の部品は上の専用パネルが編集するので一覧に出さない
                if (subObjects[k]->Name() == NS::Obj::k_TransformSubObjName)
                {
                    continue;
                }

                ImGui::PushID(static_cast<int>(k));

                // 入力 SubObject を休止させると player が動かなくなるので active を触らせない
                const bool lockedSubObject = (typeName == "PlayerInput");
                bool enabled = subObjects[k]->IsEnabled();
                ImGui::BeginDisabled(lockedSubObject);
                if (ImGui::Checkbox("##enabled", &enabled))
                {
                    editor.SetSubObjectEnabledOnSelected(*subObjects[k], enabled);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();

                // ヘッダを中身より明るくして、どこからどこまでが 1 個か見えるようにする
                ImGui::PushStyleColor(ImGuiCol_Header, NS::Editor::k_SubObjectHeaderColor);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, NS::Editor::k_SubObjectHeaderHoveredColor);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, NS::Editor::k_SubObjectHeaderActiveColor);
                std::string header{subObjects[k]->Name()};
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
                    NS::Obj::SubObject* live = subObjects[k];
                    // 比べる相手は持ち主のクラスの既定の部品。種類の既定値まで当たっている
                    const NS::Obj::SubObject* baseline = m_defaults.Find(*live);
                    const NS::Editor::SubObjectEditResult r =
                        NS::Editor::DrawReflectedSubObject(*live, refOptions, baseline, m_fieldFilter);
                    subObjectEdit.activated |= r.activated;
                    subObjectEdit.committed |= r.committed;
                    subObjectEdit.changed |= r.changed;
                    if (r.revertField != nullptr)
                    {
                        subObjectEdit.revertTarget = r.revertTarget;
                        subObjectEdit.revertField = r.revertField;
                    }
                    if (r.promoteField != nullptr)
                    {
                        subObjectEdit.promoteTarget = r.promoteTarget;
                        subObjectEdit.promoteField = r.promoteField;
                    }
                    // プレイ中の手編集は編集復帰の組み直しで消えるので、編集された欄だけ凍結側へも写す
                    if (r.changedTarget != nullptr && r.changedField != nullptr)
                    {
                        editor.MirrorPlayEditToBaseline(*r.changedTarget, r.changedField->name);
                    }
                }
                ImGui::PopID();
            }
            // 戻すは控えを取ってから live を書く。順を逆にすると変更後が控えになり履歴が空になる
            if (subObjectEdit.revertTarget != nullptr && subObjectEdit.revertField != nullptr)
            {
                const NS::Obj::SubObject* baseline = m_defaults.Find(*subObjectEdit.revertTarget);
                if (baseline != nullptr)
                {
                    editor.BeginSubObjectEdit();
                    NS::Editor::RevertFieldToDefault(
                        *subObjectEdit.revertTarget, *baseline, *subObjectEdit.revertField);
                    // 既定へ戻すのも手編集。プレイ中は凍結側へも写して残す
                    editor.MirrorPlayEditToBaseline(*subObjectEdit.revertTarget, subObjectEdit.revertField->name);
                    editor.CommitSubObjectEdit();
                }
            }
            // 種類の既定にするのは、同じ種類の全ての個体とファイルを書き換える。既定の部品を作り直すので描画の後で行う
            if (subObjectEdit.promoteTarget != nullptr && subObjectEdit.promoteField != nullptr)
            {
                (void)editor.PromoteFieldToArchetype(*subObjectEdit.promoteTarget, subObjectEdit.promoteField->name);
            }
            if (subObjectEdit.activated)
            {
                editor.BeginSubObjectEdit();
            }
            if (subObjectEdit.committed)
            {
                editor.CommitSubObjectEdit();
            }
        }
        ImGui::End();

        if (m_nameCommitId != 0)
        {
            editor.RenameObject(m_nameCommitId, m_nameBuffer);
        }
#else
        (void)editor;
#endif
    }
} // namespace NS::Editor
