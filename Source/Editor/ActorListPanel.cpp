#include "Editor/ActorListPanel.h"

#include "Editor/EditorObjects.h"
#include "Editor/EditorUi.h"
#include "Editor/LevelEditorController.h"
#include "Editor/PanelIds.h"
#include "Editor/PlacementCatalog.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/SceneActor.h"

#include "NSlib/Core/Types.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS
{
    namespace Editor
    {
#if NS_EDITOR_ENABLED
        namespace
        {

            // 置ける物の一覧をメニューに並べ、選んだ物を編集視点の中心あたりへ置く
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
#endif

        void ActorListPanel::Render(LevelEditorController& editor) noexcept
        {
#if NS_EDITOR_ENABLED
            if (ImGui::Begin(k_PanelActorList))
            {
                // 見出しに今開いているシーン名を出す。未保存なら * を付ける
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

                const std::vector<SceneActor*> placed = PlacedActors(editor.Objects());
                ImGui::Text("オブジェクト %zu 個", placed.size());

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##actorListFilter", "検索", m_actorListFilter, sizeof(m_actorListFilter));
                const bool filtering = m_actorListFilter[0] != '\0';
                ImGui::Separator();

                // 改名は配置物を組み直して一覧を入れ替えるので、描き終えるまで適用を待つ
                m_renameCommitId = 0;
                m_duplicateRequestId = 0;
                m_deleteRequestId = 0;
                m_rangeSelectToId = 0;
                m_focusPending = false;
                m_visibleOrder.clear();

                if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2))
                {
                    if (SceneActor* target = editor.SelectedObjectActor())
                    {
                        BeginRename(*target);
                    }
                }

                for (SceneActor* object : placed)
                {
                    if (!filtering || NameMatches(Editor::ObjectDisplayName(*object), m_actorListFilter))
                    {
                        RenderRow(editor, *object);
                    }
                }

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

                // 余白の右クリックからも同じ一覧で置く。描き終えた後なので配置物を組み直しても崩れない
                if (ImGui::BeginPopupContextItem("##actorListAdd"))
                {
                    RenderPlacementMenu(editor);
                    ImGui::EndPopup();
                }

                // 一覧を触り終えてから適用する。永続 id 越しなので組み直しを跨いでも同じ 1 体に届く
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
                        editor.SelectObjects(std::move(range), m_rangeSelectToId);
                    }
                }
                // 選んだ物を見失わないよう視点を寄せる。範囲選択を流した後なので選択は確定している
                if (m_focusPending)
                {
                    editor.FocusSelectedInView();
                }

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

        void ActorListPanel::RenderRow(LevelEditorController& editor, SceneActor& object) noexcept
        {
#if NS_EDITOR_ENABLED
            const uint32 id = object.Id();

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
                {
                    m_renameCommitId = id;
                }
                else if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    m_renameCommitId = id; // 枠外クリックで抜けた分も書き込む
                }
                else if (ImGui::IsItemDeactivated())
                {
                    m_renamingObjectId = 0; // Esc は入力欄が値を戻すのでそのまま捨てる
                }

                ImGui::PopID();
                return;
            }

            m_visibleOrder.push_back(id);

            ImGui::Selectable(Editor::ObjectDisplayName(object),
                              editor.IsObjectSelected(id),
                              ImGuiSelectableFlags_AllowDoubleClick);

            if (ImGui::IsItemClicked())
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
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_focusPending = true;
            }

            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("名前を変更", "F2"))
                {
                    BeginRename(object);
                }
                if (ImGui::MenuItem("フォーカス", "F"))
                {
                    editor.SelectObjectById(id);
                    editor.FocusSelectedInView();
                }
                if (ImGui::MenuItem("複製", "Ctrl+D"))
                {
                    m_duplicateRequestId = id;
                }
                if (ImGui::MenuItem("削除", "Del"))
                {
                    m_deleteRequestId = id;
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
#else
            (void)editor;
            (void)object;
#endif
        }

        void ActorListPanel::BeginRename(SceneActor& object) noexcept
        {
#if NS_EDITOR_ENABLED
            m_renamingObjectId = object.Id();
            // 導出名から始めるので、そのまま確定すると今見えている名前がそのまま実体になる
            std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", Editor::ObjectDisplayName(object));
            m_renameFocusPending = true;
#else
            (void)object;
#endif
        }
    } // namespace Editor
} // namespace NS
