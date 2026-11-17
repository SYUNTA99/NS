#include "Editor/ActorListPanel.h"

#include "Editor/EditorActors.h"
#include "Editor/EditorUi.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Editor/PlacementCatalog.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/SceneActor.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Types.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

namespace NS
{
    namespace Ed
    {
        namespace
        {
            // 一覧の行を別の行へ引きずる時の受け渡しの種類。中身は引きずった行の永続 id
            constexpr const char* k_ActorRowDragType = "NS_ACTOR_ROW";

            void RenderPlacementMenu(LevelEditorController& editor)
            {
                for (const PlacementItem& item : PlacementItems())
                {
                    if (ImGui::MenuItem(item.label.c_str()))
                    {
                        editor.PlaceItem(item);
                    }
                }
            }
        } // namespace

        void ActorListPanel::Render(LevelEditorController& editor) noexcept
        {
            if (ImGui::Begin(k_PanelActorList))
            {
                const std::string& sceneName = editor.Editor().CurrentLevelName();
                const char* shownName = "(名前なし)";
                if (!sceneName.empty())
                {
                    shownName = sceneName.c_str();
                }
                if (editor.Editor().HasUnsavedChanges())
                {
                    ImGui::Text("%s *", shownName);
                }
                else
                {
                    ImGui::TextUnformatted(shownName);
                }
                ImGui::Separator();

                if (ImGui::SmallButton("シーン設定"))
                {
                    editor.SelectSceneSettings();
                }

                const std::vector<SceneActor*> placed = PlacedActors(editor.Actors());
                ImGui::Text("アクター %zu 個", placed.size());
                ImGui::Separator();

                // 複製・削除・親の付け替えは描いている一覧を変えるので、描き終えるまで適用を待つ。改名も同じ所で当てる
                m_renameCommitId = 0;
                m_duplicateRequestId = 0;
                m_deleteRequestId = 0;
                m_reparentRequested = false;
                m_rangeSelectToId = 0;
                m_focusPending = false;
                m_visibleOrder.clear();

                if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2))
                {
                    if (SceneActor* target = editor.SelectedActor())
                    {
                        BeginRename(target);
                    }
                }

                RenderActorRows(editor, placed);

                if (placed.empty())
                {
                    ImGui::TextDisabled("(オブジェクトなし)");
                }

                // 置けるクラスの一覧から選んで置く。種類はクラスが決め、ここで部品を組むことはしない
                if (ImGui::SmallButton("+ 追加"))
                {
                    ImGui::OpenPopup("##placeActor");
                }
                if (ImGui::BeginPopup("##placeActor"))
                {
                    RenderPlacementMenu(editor);
                    ImGui::EndPopup();
                }

                ImVec2 rest = ImGui::GetContentRegionAvail();
                rest.x = std::max(rest.x, 1.0f);
                rest.y = std::max(rest.y, ImGui::GetTextLineHeight());
                ImGui::Dummy(rest);
                // 一覧の下の余白へ行を落とすと、その行の親を外す
                HandleReparentDrag(0, k_NoActorId, nullptr);

                // 余白の右クリックからも同じ一覧で置く。描き終えた後なのでアクターを組み直しても崩れない
                if (ImGui::BeginPopupContextItem("##actorListAdd"))
                {
                    RenderPlacementMenu(editor);
                    ImGui::EndPopup();
                }

                // 一覧を触り終えてから適用する。永続 id 越しなので組み直しを跨いでも同じ 1 体に届く
                if (m_renameCommitId != 0)
                {
                    editor.RenameActor(m_renameCommitId, m_renameBuffer);
                    m_renamingActorId = 0;
                }
                // Shift クリックは起点から今の行までを丸ごと選ぶ。並びは描いた順が正
                if (m_rangeSelectToId != 0)
                {
                    std::size_t from = m_visibleOrder.size();
                    std::size_t to = m_visibleOrder.size();
                    for (std::size_t i = 0; i < m_visibleOrder.size(); ++i)
                    {
                        if (m_visibleOrder[i] == m_selectionAnchorId)
                        {
                            from = i;
                        }
                        if (m_visibleOrder[i] == m_rangeSelectToId)
                        {
                            to = i;
                        }
                    }
                    if (from < m_visibleOrder.size() && to < m_visibleOrder.size())
                    {
                        if (from > to)
                        {
                            std::swap(from, to);
                        }
                        std::vector<uint32> range(m_visibleOrder.begin() + static_cast<std::ptrdiff_t>(from),
                                                      m_visibleOrder.begin() + static_cast<std::ptrdiff_t>(to) + 1);
                        editor.SelectActors(std::move(range), m_rangeSelectToId);
                    }
                }
                // 選んだ物を見失わないよう視点を寄せる。範囲選択を流した後なので選択は確定している
                if (m_focusPending)
                {
                    editor.FocusSelectedInView();
                }

                if (m_duplicateRequestId != 0)
                {
                    editor.SelectActorById(m_duplicateRequestId);
                    editor.DuplicateSelectedActor();
                }
                if (m_deleteRequestId != 0)
                {
                    editor.SelectActorById(m_deleteRequestId);
                    editor.DeleteSelectedActor();
                }
                // 自分と子孫へ落とした時・プレイ中は ReparentActor が断る。断っても一覧は次の描画で元のまま
                if (m_reparentRequested)
                {
                    (void)editor.ReparentActor(m_reparentChildId, m_reparentParentId);
                }
            }
            ImGui::End();
        }

        void ActorListPanel::RenderActorRows(LevelEditorController& editor,
                                             const std::vector<SceneActor*>& placed) noexcept
        {
            // 子は親の下に並べる。親の居ない物と、親が居なくなった物が根になる
            std::unordered_set<uint32> placedIds;
            for (SceneActor* object : placed)
            {
                placedIds.insert(object->Id());
            }
            ChildrenMap childrenOf;
            for (SceneActor* object : placed)
            {
                const uint32 parent = object->TransformObj().Parent().id;
                if (placedIds.contains(parent))
                {
                    childrenOf[parent].push_back(object);
                }
            }
            std::unordered_set<uint32> rendered;
            for (SceneActor* object : placed)
            {
                if (!placedIds.contains(object->TransformObj().Parent().id))
                {
                    RenderTree(editor, object, childrenOf, rendered);
                }
            }
            // 親子が輪になった物は根が無いので、残りを根として描く
            for (SceneActor* object : placed)
            {
                if (!rendered.contains(object->Id()))
                {
                    RenderTree(editor, object, childrenOf, rendered);
                }
            }
        }

        void ActorListPanel::RenderTree(LevelEditorController& editor,
                                        SceneActor* object,
                                        const ChildrenMap& childrenOf,
                                        std::unordered_set<uint32>& rendered) noexcept
        {
            NS_ASSERT(App, object != nullptr, "RenderTree: アクターが null");
            const uint32 id = object->Id();
            rendered.insert(id);
            std::vector<SceneActor*> children;
            const ChildrenMap::const_iterator found = childrenOf.find(id);
            if (found != childrenOf.end())
            {
                for (SceneActor* child : found->second)
                {
                    if (!rendered.contains(child->Id()))
                    {
                        children.push_back(child);
                    }
                }
            }

            ImGui::PushID(static_cast<int>(id));
            if (RenderRow(editor, object, !children.empty()))
            {
                for (SceneActor* child : children)
                {
                    // 前の兄弟の子孫として描いた物は飛ばす (輪の時だけ起きる)
                    if (!rendered.contains(child->Id()))
                    {
                        RenderTree(editor, child, childrenOf, rendered);
                    }
                }
                ImGui::TreePop();
            }
            else
            {
                // 畳んだ子孫も描いた事にする。輪の残りを根として描く所で、畳んだ子孫が根に出ないようにする
                MarkDescendantsRendered(children, childrenOf, rendered);
            }
            ImGui::PopID();
        }

        void ActorListPanel::MarkDescendantsRendered(const std::vector<SceneActor*>& children,
                                                     const ChildrenMap& childrenOf,
                                                     std::unordered_set<uint32>& rendered) noexcept
        {
            for (SceneActor* child : children)
            {
                if (!rendered.insert(child->Id()).second)
                {
                    continue;
                }
                const ChildrenMap::const_iterator found = childrenOf.find(child->Id());
                if (found != childrenOf.end())
                {
                    MarkDescendantsRendered(found->second, childrenOf, rendered);
                }
            }
        }

        void ActorListPanel::HandleReparentDrag(uint32 rowId, uint32 parent, const char* label) noexcept
        {
            if (rowId != 0 && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(k_ActorRowDragType, &rowId, sizeof(rowId));
                ImGui::TextUnformatted(label);
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(k_ActorRowDragType))
                {
                    std::memcpy(&m_reparentChildId, payload->Data, sizeof(m_reparentChildId));
                    m_reparentParentId = parent;
                    m_reparentRequested = true;
                }
                ImGui::EndDragDropTarget();
            }
        }

        bool ActorListPanel::RenderRow(LevelEditorController& editor, SceneActor* object, bool hasChildren) noexcept
        {
            NS_ASSERT(App, object != nullptr, "RenderRow: アクターが null");
            const uint32 id = object->Id();

            if (m_renamingActorId != 0 && m_renamingActorId == id)
            {
                if (m_renameFocusPending)
                {
                    ImGui::SetKeyboardFocusHere();
                    m_renameFocusPending = false;
                }
                constexpr ImGuiInputTextFlags k_RenameFlags =
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
                if (ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer), k_RenameFlags))
                {
                    m_renameCommitId = id;
                }
                else if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    m_renameCommitId = id; // 枠外クリックで抜けた分も書き込む
                }
                else if (ImGui::IsItemDeactivated())
                {
                    m_renamingActorId = 0; // Esc は入力欄が値を戻すのでそのまま捨てる
                }
                // 改名の間は子の行を畳む
                return false;
            }

            m_visibleOrder.push_back(id);

            // 子を持つ行は矢印で開け閉めする。行の文字を押すと選ぶ
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_DefaultOpen;
            if (!hasChildren)
            {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }
            if (editor.IsActorSelected(id))
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            const char* label = Ed::ActorDisplayName(object);
            const bool open = ImGui::TreeNodeEx("##row", flags, "%s", label);

            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                const ImGuiIO& io = ImGui::GetIO();
                if (io.KeyCtrl)
                {
                    editor.ToggleActorSelection(id);
                    m_selectionAnchorId = id;
                }
                else if (io.KeyShift && m_selectionAnchorId != 0)
                {
                    m_rangeSelectToId = id;
                }
                else
                {
                    editor.SelectActorById(id);
                    m_selectionAnchorId = id;
                }
            }
            // ダブルクリックで視点を寄せる。改名は F2 と右クリックのメニューから
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_focusPending = true;
            }
            // 引きずる元は見本の小窓を開くので、行のクリックと視点を寄せる判定を済ませてから置く
            HandleReparentDrag(id, id, label);

            RenderRowMenu(editor, object);
            // 子の無い行は NoTreePushOnOpen なので、開いていても積まれていない
            return open && hasChildren;
        }

        void ActorListPanel::RenderRowMenu(LevelEditorController& editor, SceneActor* object) noexcept
        {
            if (!ImGui::BeginPopupContextItem())
            {
                return;
            }
            const uint32 id = object->Id();
            if (ImGui::MenuItem("名前を変更", "F2"))
            {
                BeginRename(object);
            }
            if (ImGui::MenuItem("フォーカス", "F"))
            {
                editor.SelectActorById(id);
                editor.FocusSelectedInView();
            }
            if (ImGui::MenuItem("複製", "Ctrl+D"))
            {
                m_duplicateRequestId = id;
            }
            // 一覧が長いと下の余白へ落としにくいので、親を外す操作をここにも置く
            if (ImGui::MenuItem("親を外す", nullptr, false, object->TransformObj().Parent().IsSet()))
            {
                m_reparentChildId = id;
                m_reparentParentId = k_NoActorId;
                m_reparentRequested = true;
            }
            if (ImGui::MenuItem("削除", "Del"))
            {
                m_deleteRequestId = id;
            }
            ImGui::EndPopup();
        }

        void ActorListPanel::BeginRename(SceneActor* object) noexcept
        {
            NS_ASSERT(App, object != nullptr, "BeginRename: アクターが null");
            m_renamingActorId = object->Id();
            // 導出名から始めるので、そのまま確定すると今見えている名前がそのまま実体になる
            std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", Ed::ActorDisplayName(object));
            m_renameFocusPending = true;
        }
    } // namespace Ed
} // namespace NS
