#include "Editor/ConsolePanel.h"

#include "Editor/PanelIds.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Windows/Clock.h"

#if NS_EDITOR_ENABLED
#include "Editor/EditorUi.h"

#include <imgui.h>

#include <vector>
#endif

namespace NS::Editor
{
        void ConsolePanel::Render() noexcept
        {
#if NS_EDITOR_ENABLED
            if (ImGui::Begin(k_PanelConsole))
            {
                const ImGuiIO& io = ImGui::GetIO();
                const float fps = io.Framerate;
                float ms = 0.0f;
                if (fps > 0.0f)
                {
                    ms = 1000.0f / fps;
                }
                ImGui::Text("%.1f FPS (%.2f ms)", static_cast<double>(fps), static_cast<double>(ms));
                ImGui::Text("delta       : %.4f s", static_cast<double>(OS::FrameTimer::DeltaSeconds()));
                ImGui::Text("fixed delta : %.4f s", static_cast<double>(OS::FrameTimer::FixedDelta()));
                ImGui::Text("fixed steps : %d", OS::FrameTimer::FixedStepsThisFrame());

                ImGui::Separator();
                const std::vector<RecentLog> warnings = Logger::RecentWarnings();
                ImGui::Text("警告とエラー: %d 件 (新しい順)", static_cast<int>(warnings.size()));
                if (ImGui::BeginChild("##recent_warnings", ImVec2{0.0f, 0.0f}, ImGuiChildFlags_Borders))
                {
                    for (const RecentLog& log : warnings)
                    {
                        ImVec4 color = k_MsgErrorColor;
                        if (log.level == LogLevel::Warn)
                        {
                            color = k_MsgWarnColor;
                        }
                        ImGui::PushStyleColor(ImGuiCol_Text, color);
                        ImGui::TextWrapped("%s", log.text.c_str());
                        ImGui::PopStyleColor();
                    }
                }
                ImGui::EndChild();
            }
            ImGui::End();
#endif
        }
} // namespace NS::Editor
