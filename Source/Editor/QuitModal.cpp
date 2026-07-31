#include "Editor/QuitModal.h"

#include "Editor/EditorUi.h"
#include "Editor/LevelEditorController.h"
#include "Runtime/App/Application.h"

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    bool QuitModal::RequestQuit() noexcept
    {
        if (m_confirmed)
            return true;
        // まだ確認していない終了要求は modal を開いて握りつぶす。 取り下げを Application に返す
        m_open = true;
        m_saveFailed = false;
        return false;
    }

    void QuitModal::Render(LevelEditorController& editor) noexcept
    {
#if NS_EDITOR_ENABLED
        if (!m_open)
            return;
        const auto vp = ImGui::GetMainViewport();
        if (vp != nullptr)
        {
            ImGui::SetNextWindowPos(
                ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                ImGuiCond_Always,
                ImVec2(0.5f, 0.5f));
        }
        constexpr ImGuiWindowFlags k_QuitModalFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("終了の確認", nullptr, k_QuitModalFlags))
        {
            ImGui::TextUnformatted("変更を保存して終了しますか");
            ImGui::Separator();
            if (ImGui::Button("保存して終了", ImVec2(180.0f, 0.0f)))
            {
                // 保存成功でのみ終了する。 失敗時は modal を残しデータ消失を防ぐ
                if (editor.Editor().SaveForQuit())
                {
                    m_confirmed = true;
                    m_open = false;
                    NS::App::Application::Quit();
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
                NS::App::Application::Quit();
            }
            if (ImGui::Button("キャンセル", ImVec2(180.0f, 0.0f)))
                m_open = false;
            if (m_saveFailed)
                ImGui::TextColored(k_MsgErrorColor, "保存に失敗しました");
        }
        ImGui::End();
#else
        (void)editor;
#endif
    }
} // namespace NS::Editor
