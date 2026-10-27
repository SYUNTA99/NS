#include "Editor/QuitModal.h"

#include "Editor/EditorMode.h"
#include "Editor/EditorUi.h"
#include "Editor/ActionTimelinePanel.h"
#include "Editor/LevelEditorController.h"
#include "NSlib/App/Application.h"

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
        bool HasUnsavedWork(const EditorMode& editor, const ActionTimelinePanel& timeline) noexcept
        {
            return editor.HasUnsavedChanges() || timeline.HasUnsavedChanges();
        }

        bool SaveAllForQuit(EditorMode& editor, ActionTimelinePanel& timeline) noexcept
        {
            // シーンは変更があるか問わず書く。プレイ中にインスペクタで変えた値は履歴の版に載らないため
            const bool sceneSaved = editor.SaveForQuit();
            bool timelineSaved = true;
            if (timeline.HasUnsavedChanges())
            {
                timelineSaved = timeline.SaveAll();
            }
            return sceneSaved && timelineSaved;
        }

        bool QuitModal::RequestQuit() noexcept
        {
            if (m_confirmed)
            {
                return true;
            }
            // まだ確認していない終了要求は modal を開いて握りつぶす。取り下げを Application に返す
            m_open = true;
            m_saveFailed = false;
            return false;
        }

        void QuitModal::Render(LevelEditorController& editor, ActionTimelinePanel& timeline) noexcept
        {
#if NS_EDITOR_ENABLED
            if (!m_open)
            {
                return;
            }
            EditorMode& mode = editor.Editor();
            // モーダルにして、開いている間は編集のショートカットと他の窓の操作を止める。
            // 他のモーダル (Save Level など) が開いていても、同じ段で開き直して前に出す
            constexpr const char* k_QuitModalTitle = "終了の確認";
            if (!ImGui::IsPopupOpen(k_QuitModalTitle))
            {
                ImGui::OpenPopup(k_QuitModalTitle);
            }
            ImGuiViewport* const vp = ImGui::GetMainViewport();
            if (vp != nullptr)
            {
                ImGui::SetNextWindowPos(vp->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            }
            constexpr ImGuiWindowFlags k_QuitModalFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                          ImGuiWindowFlags_NoCollapse |
                                                          ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::BeginPopupModal(k_QuitModalTitle, nullptr, k_QuitModalFlags))
            {
                ImGui::TextUnformatted("変更を保存して終了しますか");
                if (HasUnsavedWork(mode, timeline))
                {
                    ImGui::TextUnformatted("保存していない変更:");
                    if (mode.HasUnsavedChanges())
                    {
                        ImGui::BulletText("シーン");
                    }
                    if (timeline.HasUnsavedChanges())
                    {
                        ImGui::BulletText("動作のタイムライン");
                    }
                }
                ImGui::Separator();
                const bool playing = editor.CurrentMode() == LevelEditorController::Mode::Play;
                ImGui::BeginDisabled(playing);
                const bool saveClicked = ImGui::Button("保存して終了", ImVec2(180.0f, 0.0f));
                ImGui::EndDisabled();
                if (playing)
                {
                    ImGui::TextColored(k_MsgErrorColor, "プレイ中は保存できません。編集へ戻ってから保存してください");
                }
                if (saveClicked)
                {
                    // 保存成功でのみ終了する。失敗時は modal を残しデータ消失を防ぐ
                    if (SaveAllForQuit(mode, timeline))
                    {
                        m_confirmed = true;
                        m_open = false;
                        Application::Quit();
                    }
                    else
                    {
                        m_saveFailed = true;
                    }
                }
                if (ImGui::Button("保存せず終了", ImVec2(180.0f, 0.0f)))
                {
                    m_confirmed = true;
                    m_open = false;
                    Application::Quit();
                }
                if (ImGui::Button("キャンセル", ImVec2(180.0f, 0.0f)))
                {
                    m_open = false;
                }
                if (m_saveFailed)
                {
                    ImGui::TextColored(k_MsgErrorColor, "保存に失敗しました");
                }
                if (!m_open)
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
#else
            (void)editor;
            (void)timeline;
#endif
        }
} // namespace NS::Editor
