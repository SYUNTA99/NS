#include "Editor/HierarchyPanel.h"

#include "Editor/EditorObjects.h"
#include "Editor/EditorUi.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/World.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
#if NS_EDITOR_ENABLED
    namespace
    {
        // ヒエラルキー内で配置物をドラッグする時の荷札
        constexpr const char* k_HierarchyDragType = "NS_HIERARCHY_OBJECT";
    } // namespace
#endif

    void HierarchyPanel::Render(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        if (ImGui::Begin(k_PanelHierarchy))
        {
            // 見出しに今開いているシーン名を出す。 未保存なら * を付ける
            const std::string& sceneName = editor.Editor().CurrentLevelName();
            const char* shownName = "(名前なし)";
            if (!sceneName.empty())
                shownName = sceneName.c_str();
            if (editor.Editor().HasUnsavedChanges())
                ImGui::Text("%s *", shownName);
            else
                ImGui::TextUnformatted(shownName);
            ImGui::Separator();

            std::size_t shownCount = 0;
            for (const NS::Object::GameObject* obj : editor.World())
                if (!obj->IsTransient())
                    ++shownCount;
            ImGui::Text("オブジェクト %zu 個", shownCount);

            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##hierarchyFilter", "検索", m_hierarchyFilter, sizeof(m_hierarchyFilter));
            const bool filtering = m_hierarchyFilter[0] != '\0';
            ImGui::Separator();

            // 改名も親の付け替えも world を組み直して objects を入れ替えるので、木を描き終えるまで適用を待つ
            m_renameCommitId = 0;
            m_reparentPending = false;
            m_duplicateRequestId = 0;
            m_deleteRequestId = 0;
            m_rangeSelectToId = 0;
            m_focusPending = false;
            m_visibleOrder.clear();

            if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2))
            {
                if (NS::Object::GameObject* target = editor.SelectedObjectGameObject())
                    BeginRename(*target);
            }

            // 検索中は木を畳んで、一致した物だけを親子に関係なく並べる
            for (NS::Object::GameObject* objPtr : editor.World())
            {
                NS::Object::GameObject& object = *objPtr;
                // 一時オブジェクトは配置物でないので一覧に出さない
                if (object.IsTransient())
                    continue;
                if (filtering)
                {
                    if (NameMatches(NS::Editor::ObjectDisplayName(object), m_hierarchyFilter))
                        RenderNode(editor, object, false);
                    continue;
                }
                // root から潜る。子は各ノードが自分で辿る
                if (object.Parent() == nullptr)
                    RenderNode(editor, object, true);
            }

            if (shownCount == 0)
                ImGui::TextDisabled("(オブジェクトなし)");

            if (ImGui::SmallButton("+ オブジェクトを追加"))
                editor.AddObject();

            // 余白へ落としたら root へ戻す受け皿
            ImVec2 rest = ImGui::GetContentRegionAvail();
            rest.x = std::max(rest.x, 1.0f);
            rest.y = std::max(rest.y, ImGui::GetTextLineHeight());
            ImGui::Dummy(rest);
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(k_HierarchyDragType))
                {
                    std::uint32_t dragged = 0;
                    std::memcpy(&dragged, payload->Data, sizeof(dragged));
                    m_reparentChildId = dragged;
                    m_reparentParentId = 0;
                    m_reparentPending = true;
                }
                ImGui::EndDragDropTarget();
            }

            // 余白の右クリックから基本形を足す。木を描き終えた後なので world を組み直しても崩れない
            if (ImGui::BeginPopupContextItem("##hierarchyAdd"))
            {
                if (ImGui::MenuItem("空のオブジェクト"))
                    editor.AddPrimitive(NS::Editor::PrimitiveKind::Empty);
                if (ImGui::MenuItem("立方体"))
                    editor.AddPrimitive(NS::Editor::PrimitiveKind::Cube);
                if (ImGui::MenuItem("球"))
                    editor.AddPrimitive(NS::Editor::PrimitiveKind::Sphere);
                if (ImGui::MenuItem("坂"))
                    editor.AddPrimitive(NS::Editor::PrimitiveKind::Slope);
                ImGui::EndPopup();
            }

            // objects を触り終えてから適用する。永続 id 越しなので組み直しを跨いでも同じ 1 体に届く
            if (m_renameCommitId != 0)
            {
                editor.RenameObject(m_renameCommitId, m_renameBuffer);
                m_renamingObjectId = 0;
            }
            // Shift クリックは起点から今の行までを丸ごと選ぶ。並びは描いた順が正
            if (m_rangeSelectToId != 0)
            {
                std::size_t from = m_visibleOrder.size();
                std::size_t to = m_visibleOrder.size();
                for (std::size_t i = 0; i < m_visibleOrder.size(); ++i)
                {
                    if (m_visibleOrder[i] == m_selectionAnchorId)
                        from = i;
                    if (m_visibleOrder[i] == m_rangeSelectToId)
                        to = i;
                }
                if (from < m_visibleOrder.size() && to < m_visibleOrder.size())
                {
                    if (from > to)
                        std::swap(from, to);
                    std::vector<std::uint32_t> range;
                    range.reserve(to - from + 1);
                    for (std::size_t i = from; i <= to; ++i)
                        range.push_back(m_visibleOrder[i]);
                    editor.SelectObjects(std::move(range), m_rangeSelectToId);
                }
            }
            // 選んだ物を見失わないよう視点を寄せる。範囲選択を流した後なので選択は確定している
            if (m_focusPending)
                editor.FocusSelectedInView();

            if (m_reparentPending && editor.SetObjectParent(m_reparentChildId, m_reparentParentId))
                m_expandParentId = m_reparentParentId;
            if (m_duplicateRequestId != 0)
            {
                editor.SelectObjectById(m_duplicateRequestId);
                editor.DuplicateSelectedObject();
            }
            if (m_deleteRequestId != 0)
            {
                editor.SelectObjectById(m_deleteRequestId);
                editor.DeleteSelectedObject();
            }
        }
        ImGui::End();
#else
        (void)editor;
#endif
    }

    void HierarchyPanel::RenderNode(LevelEditorController& editor,
                                    NS::Object::GameObject& object,
                                    bool withChildren) noexcept
    {
#if NS_EDITOR_ENABLED
        const std::uint32_t id = object.Id();

        bool hasChildren = false;
        if (withChildren)
        {
            for (const NS::Object::GameObject* child : object.Children())
            {
                if (child != nullptr && !child->IsTransient())
                {
                    hasChildren = true;
                    break;
                }
            }
        }

        ImGui::PushID(static_cast<int>(id));

        if (m_renamingObjectId != 0 && m_renamingObjectId == id)
        {
            if (m_renameFocusPending)
            {
                ImGui::SetKeyboardFocusHere();
                m_renameFocusPending = false;
            }
            constexpr ImGuiInputTextFlags k_RenameFlags =
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
            if (ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer), k_RenameFlags))
                m_renameCommitId = id;
            else if (ImGui::IsItemDeactivatedAfterEdit())
                m_renameCommitId = id; // 枠外クリックで抜けた分も書き込む
            else if (ImGui::IsItemDeactivated())
                m_renamingObjectId = 0; // Esc は入力欄が値を戻すのでそのまま捨てる

            // 改名中は子を畳んでおく。入力欄に木の開閉を持たせない
            ImGui::PopID();
            return;
        }

        // 落とした先が閉じていると子が中に隠れて消えたように見えるので、その 1 体だけ開いてやる
        if (m_expandParentId != 0 && m_expandParentId == id)
        {
            ImGui::SetNextItemOpen(true);
            m_expandParentId = 0;
        }

        // 開閉は矢印だけに任せる。行のクリックは選択、ダブルクリックは視点寄せに空けておく
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (editor.IsObjectSelected(id))
            flags |= ImGuiTreeNodeFlags_Selected;

        // 範囲選択は行の並びが要るので、描いた順を控えておく
        m_visibleOrder.push_back(id);

        const bool open = ImGui::TreeNodeEx("##node", flags, "%s", NS::Editor::ObjectDisplayName(object));

        // 開閉の矢印を押しただけの時は選択を動かさない
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl)
            {
                editor.ToggleObjectSelection(id);
                m_selectionAnchorId = id;
            }
            else if (io.KeyShift && m_selectionAnchorId != 0)
            {
                m_rangeSelectToId = id;
            }
            else
            {
                editor.SelectObjectById(id);
                m_selectionAnchorId = id;
            }
        }
        // ダブルクリックで視点を寄せる。改名は F2 と右クリックのメニューから
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            m_focusPending = true;

        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(k_HierarchyDragType, &id, sizeof(id));
            ImGui::TextUnformatted(NS::Editor::ObjectDisplayName(object));
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(k_HierarchyDragType))
            {
                std::uint32_t dragged = 0;
                std::memcpy(&dragged, payload->Data, sizeof(dragged));
                m_reparentChildId = dragged;
                m_reparentParentId = id;
                m_reparentPending = true;
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("名前を変更", "F2"))
                BeginRename(object);
            if (ImGui::MenuItem("フォーカス", "F"))
            {
                editor.SelectObjectById(id);
                editor.FocusSelectedInView();
            }
            if (ImGui::MenuItem("複製", "Ctrl+D"))
                m_duplicateRequestId = id;
            if (ImGui::MenuItem("削除", "Del"))
                m_deleteRequestId = id;
            if (ImGui::MenuItem("親子を解除", nullptr, false, object.Parent() != nullptr))
            {
                m_reparentChildId = id;
                m_reparentParentId = 0;
                m_reparentPending = true;
            }
            ImGui::EndPopup();
        }

        if (open && hasChildren)
        {
            for (NS::Object::GameObject* child : object.Children())
            {
                if (child != nullptr && !child->IsTransient())
                    RenderNode(editor, *child, true);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
#else
        (void)editor;
        (void)object;
        (void)withChildren;
#endif
    }

    void HierarchyPanel::BeginRename(NS::Object::GameObject& object) noexcept
    {
#if NS_EDITOR_ENABLED
        m_renamingObjectId = object.Id();
        // 導出名から始めるので、そのまま確定すると今見えている名前がそのまま実体になる
        std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", NS::Editor::ObjectDisplayName(object));
        m_renameFocusPending = true;
#else
        (void)object;
#endif
    }
} // namespace NS::Editor
