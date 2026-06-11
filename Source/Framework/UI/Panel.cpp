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
        // NewFrame 未呼出だと GetCurrentContext==nullptr で Begin が assert 死するので守る
        if (::ImGui::GetCurrentContext() == nullptr)
        {
            (void)title;
            (void)isOpen;
            return;
        }
        // Begin は null-terminated 必須なので std::string 経由で渡す
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
