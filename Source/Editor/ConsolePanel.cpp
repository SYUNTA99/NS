#include "Editor/ConsolePanel.h"

#include "Editor/PanelIds.h"
#include "NSlib/Windows/Clock.h"

#if NS_EDITOR_ENABLED
#include <imgui.h>
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
            const float ms = [fps]() -> float {
                if (fps > 0.0f)
                {
                    return 1000.0f / fps;
                }
                return 0.0f;
            }();
            ImGui::Text("%.1f FPS (%.2f ms)", static_cast<double>(fps), static_cast<double>(ms));
            ImGui::Text("delta       : %.4f s", static_cast<double>(NS::OS::FrameTimer::DeltaSeconds()));
            ImGui::Text("fixed delta : %.4f s", static_cast<double>(NS::OS::FrameTimer::FixedDelta()));
            ImGui::Text("fixed steps : %d", NS::OS::FrameTimer::FixedStepsThisFrame());
        }
        ImGui::End();
#endif
    }
} // namespace NS::Editor
