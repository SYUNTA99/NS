#include "Runtime/UI/UISystem.h"

#include "Runtime/Graphics/Renderer.h"

namespace NS::UI
{

    UISystem::UISystem()
    {
        m_root.SetStretch(true);
    }

    void UISystem::Layout(float viewportWidth, float viewportHeight) noexcept
    {
        if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
        {
            m_scale = 1.0f;
            m_canvas = WidgetRect{};
            m_root.Layout(m_canvas);
            return;
        }

        m_scale = viewportHeight / k_ReferenceHeight;
        m_canvas = WidgetRect{0.0f, 0.0f, viewportWidth / m_scale, k_ReferenceHeight};
        m_root.Layout(m_canvas);
    }

    void UISystem::Render(NS::Graphics::Renderer& renderer)
    {
        if (!m_root.HasChildren())
            return;

        const NS::Core::Size2D size = renderer.Size();
        Layout(static_cast<float>(size.width), static_cast<float>(size.height));
        m_root.Draw(renderer, 1.0f, m_scale);
    }

    bool UISystem::ConsumesPointer(float px, float py) const noexcept
    {
        if (m_scale <= 0.0f)
            return false;
        return m_root.HitTest(px / m_scale, py / m_scale);
    }

} // namespace NS::UI
