#include "Editor/LevelFileBrowser.h"

#include "Editor/EditorUi.h"
#include "Editor/LevelFilePaths.h"

#include <algorithm>
#include <cstring>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    namespace
    {
        // ImGui InputText で確保する name buffer の容量。sanitize 側の上限 200 + 余裕
        constexpr std::size_t k_InputBufferCapacity = 256;

#if NS_EDITOR_ENABLED
        void DrawResultMessage(const std::string& message, bool error) noexcept
        {
            if (error)
            {
                ImGui::TextColored(k_MsgErrorColor, "%s", message.c_str());
            }
            else
            {
                ImGui::TextColored(k_MsgOkColor, "%s", message.c_str());
            }
        }
#endif
    } // namespace

    void LevelFileBrowser::OpenSaveModal(std::string_view initialName) noexcept
    {
        m_saveModalOpen = true;
        m_lastMessage.clear();
        m_lastMessageError = false;
        if (!initialName.empty())
        {
            m_saveNameBuffer.assign(initialName.begin(), initialName.end());
        }
        else if (m_saveNameBuffer.empty())
        {
            m_saveNameBuffer = "Scenes/new_scene";
        }
    }

    void LevelFileBrowser::OpenLoadModal() noexcept
    {
        m_loadModalOpen = true;
        m_lastMessage.clear();
        m_lastMessageError = false;
        m_loadFileList = EnumerateLevelFiles();
        if (m_loadFileList.empty())
        {
            m_loadSelection = -1;
        }
        else
        {
            m_loadSelection = 0;
        }
    }

    void LevelFileBrowser::NotifySaveResult(bool ok, std::string_view message) noexcept
    {
        m_lastMessage.assign(message.begin(), message.end());
        m_lastMessageError = !ok;
        if (ok)
        {
            m_saveModalOpen = false;
        }
    }

    void LevelFileBrowser::NotifyLoadResult(bool ok, std::string_view message) noexcept
    {
        m_lastMessage.assign(message.begin(), message.end());
        m_lastMessageError = !ok;
        if (ok)
        {
            m_loadModalOpen = false;
        }
    }

    LevelFileBrowser::Result LevelFileBrowser::Render() noexcept
    {
        Result result;
#if NS_EDITOR_ENABLED
        // 保存ダイアログ
        if (m_saveModalOpen)
        {
            ImGuiViewport* const vp = ImGui::GetMainViewport();
            if (vp != nullptr)
            {
                ImGui::SetNextWindowPos(vp->GetWorkCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            }
            ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_Appearing);
            constexpr ImGuiWindowFlags k_SaveModalFlags =
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin("Save Level", &m_saveModalOpen, k_SaveModalFlags))
            {
                ImGui::TextUnformatted("Level path (A-Za-z0-9 _-, / for subfolders)");
                // ImGui に書かせるのは size の内側だけにする
                m_saveNameBuffer.resize(std::max(m_saveNameBuffer.size() + 1, k_InputBufferCapacity));
                ImGui::InputText("##name", m_saveNameBuffer.data(), m_saveNameBuffer.size());
                m_saveNameBuffer.resize(std::strlen(m_saveNameBuffer.c_str()));

                if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)))
                {
                    const std::string sanitized = SanitizeLevelPath(m_saveNameBuffer);
                    if (sanitized.empty())
                    {
                        m_lastMessage = "不正な path (英数字/空白/_-/、 各階層 200 char 以内)";
                        m_lastMessageError = true;
                    }
                    else
                    {
                        result.action = Action::RequestSave;
                        result.targetName = sanitized;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
                {
                    m_saveModalOpen = false;
                }

                if (!m_lastMessage.empty())
                {
                    ImGui::Separator();
                    DrawResultMessage(m_lastMessage, m_lastMessageError);
                }
            }
            ImGui::End();
        }

        // 読込ダイアログ
        if (m_loadModalOpen)
        {
            ImGuiViewport* const vp = ImGui::GetMainViewport();
            if (vp != nullptr)
            {
                ImGui::SetNextWindowPos(vp->GetWorkCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            }
            ImGui::SetNextWindowSize(ImVec2(360.0f, 320.0f), ImGuiCond_Appearing);
            constexpr ImGuiWindowFlags k_LoadModalFlags = ImGuiWindowFlags_NoCollapse;
            if (ImGui::Begin("Load Level", &m_loadModalOpen, k_LoadModalFlags))
            {
                if (m_loadFileList.empty())
                {
                    ImGui::TextUnformatted("Assets/ に scene がありません");
                }
                else
                {
                    if (ImGui::BeginListBox("##levels",
                                            ImVec2(-FLT_MIN, -3.0f * ImGui::GetTextLineHeightWithSpacing())))
                    {
                        for (int i = 0; i < static_cast<int>(m_loadFileList.size()); ++i)
                        {
                            const bool selected = (m_loadSelection == i);
                            if (ImGui::Selectable(m_loadFileList[i].c_str(), selected))
                            {
                                m_loadSelection = i;
                            }
                            if (selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndListBox();
                    }
                }

                ImGui::Separator();
                const bool canLoad =
                    (m_loadSelection >= 0 && m_loadSelection < static_cast<int>(m_loadFileList.size()));
                if (!canLoad)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Load", ImVec2(120.0f, 0.0f)) && canLoad)
                {
                    result.action = Action::RequestLoad;
                    result.targetName = m_loadFileList[static_cast<std::size_t>(m_loadSelection)];
                }
                if (!canLoad)
                {
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
                {
                    m_loadModalOpen = false;
                }

                if (!m_lastMessage.empty())
                {
                    DrawResultMessage(m_lastMessage, m_lastMessageError);
                }
            }
            ImGui::End();
        }
#endif
        return result;
    }

} // namespace NS::Editor