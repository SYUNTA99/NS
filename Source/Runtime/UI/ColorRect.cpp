#include "Runtime/UI/ColorRect.h"

#include "Runtime/Graphics/Renderer.h"

namespace NS::UI
{

    void ColorRect::OnDraw(NS::Graphics::Renderer& renderer, const WidgetRect& rectPx, float alpha)
    {
        const float effective = m_color.A() * alpha;
        if (effective <= 0.0f)
            return;
        renderer.DrawScreenRect(rectPx.x,
                                rectPx.y,
                                rectPx.width,
                                rectPx.height,
                                NS::Math::Color{m_color.R(), m_color.G(), m_color.B(), effective});
    }

} // namespace NS::UI
