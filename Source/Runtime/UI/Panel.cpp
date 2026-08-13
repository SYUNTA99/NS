#include "Runtime/UI/Panel.h"

#if NS_EDITOR_ENABLED
#include <imgui.h>
#define NS_UI_IMGUI_ENABLED 1
#else
#define NS_UI_IMGUI_ENABLED 0
#endif

namespace NS::UI
{

    Panel::Panel(std::string_view title, bool* isOpen, int windowFlags) noexcept
    {
#if NS_UI_IMGUI_ENABLED
        // コンテキスト未作成で Begin を呼ぶと GImGui の null 参照で落ちる
        if (::ImGui::GetCurrentContext() == nullptr)
        {
            (void)title;
            (void)isOpen;
            (void)windowFlags;
            return;
        }
        // Begin は null 終端の文字列が要るので std::string 経由で渡す
        const std::string label(title);
        m_isOpen = ::ImGui::Begin(label.c_str(), isOpen, static_cast<ImGuiWindowFlags>(windowFlags));
        m_began = true;
#else
        (void)title;
        (void)isOpen;
        (void)windowFlags;
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
