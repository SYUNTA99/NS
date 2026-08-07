#include "Editor/ViewportSurface.h"

#include "Runtime/Graphics/RenderTarget.h"
#include "Runtime/Object/Scene/Scene.h"

#include <cstdint>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{
    ViewportSurface::ViewportSurface() = default;
    ViewportSurface::~ViewportSurface() = default;

    bool ViewportSurface::BeginView(const char* windowName, ImVec2& outMin, ImVec2& outMax, bool& outHovered) noexcept
    {
#if NS_EDITOR_ENABLED
        outHovered = false;
        outMin = ImVec2{0.0f, 0.0f};
        outMax = ImVec2{0.0f, 0.0f};

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        constexpr ImGuiWindowFlags k_PanelFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        const bool open = ImGui::Begin(windowName, nullptr, k_PanelFlags);

        bool imaged = false;
        m_visible = false;
        if (open)
        {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x >= 1.0f && avail.y >= 1.0f)
            {
                // 縦横比の指定があれば収まる最大の大きさへ縮める。 余った分は貼らずに残し黒帯にする
                ImVec2 draw = avail;
                if (m_fixedAspect > 0.0f)
                {
                    if (avail.x > avail.y * m_fixedAspect)
                        draw = ImVec2{avail.y * m_fixedAspect, avail.y};
                    else
                        draw = ImVec2{avail.x, avail.x / m_fixedAspect};
                }

                // 可視なら次フレームの描画先サイズを立てる。 RT 未生成でも記録する
                m_visible = true;
                m_size = NS::Core::Size2D{static_cast<int>(draw.x), static_cast<int>(draw.y)};

                if (m_target != nullptr && m_target->IsValid())
                {
                    // 黒帯を左右上下へ均等に割るため、 貼る直前に中央へ寄せる
                    const ImVec2 cursor = ImGui::GetCursorPos();
                    ImGui::SetCursorPos(
                        ImVec2{cursor.x + (avail.x - draw.x) * 0.5f, cursor.y + (avail.y - draw.y) * 0.5f});
                    ImGui::Image(
                        static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(m_target->UiTextureHandle())), draw);
                    outHovered = ImGui::IsItemHovered();
                    outMin = ImGui::GetItemRectMin();
                    outMax = ImGui::GetItemRectMax();
                    imaged = true;
                }
            }
        }
        return imaged;
#else
        (void)windowName;
        (void)outMin;
        (void)outMax;
        (void)outHovered;
        return false;
#endif
    }

    void ViewportSurface::EndView() noexcept
    {
#if NS_EDITOR_ENABLED
        ImGui::End();
        ImGui::PopStyleVar();
#endif
    }

    std::optional<NS::Object::SceneView> ViewportSurface::CollectView(
        std::optional<NS::Object::CameraPose> pose) noexcept
    {
        if (!m_visible || m_size.width < 8 || m_size.height < 8)
            return std::nullopt;

        if (!m_target)
            m_target = NS::Graphics::RenderTarget::Create(m_size);
        else
            m_target->Resize(m_size);

        return NS::Object::SceneView{m_target.get(), pose};
    }

    void ViewportSurface::Release() noexcept
    {
        m_target.reset();
    }
} // namespace NS::Editor
