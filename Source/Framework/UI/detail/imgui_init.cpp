#include "Framework/UI/Panel.h"

#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
#include <imgui.h>
#define NS_UI_IMGUI_ENABLED 1
#else
#define NS_UI_IMGUI_ENABLED 0
#endif

#include <string>

namespace NS::UI
{

    Panel::Panel(std::string_view title, bool* isOpen) noexcept
    {
#if NS_UI_IMGUI_ENABLED
        // ImGuiContext が未構築 / NewFrame 未呼出だと GetCurrentContext は nullptr。
        // この状態で ImGui::Begin を呼ぶと assert で死ぬので runtime check で守る。
        if (::ImGui::GetCurrentContext() == nullptr)
        {
            (void)title;
            (void)isOpen;
            return;
        }
        // ImGui::Begin は null-terminated 必須なので一旦 std::string で copy する。
        const std::string label(title);
        m_isOpen = ::ImGui::Begin(label.c_str(), isOpen);
        m_began = true;
#else
        (void)title;
        (void)isOpen;
#endif
    }

    Panel::~Panel() noexcept
    {
#if NS_UI_IMGUI_ENABLED
        if (m_began)
        {
            ::ImGui::End();
        }
#endif
    }

} // namespace NS::UI
