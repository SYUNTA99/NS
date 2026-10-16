#include "NSlib/UI/ColorRect.h"

#include "NSlib/Graphics/Renderer.h"

namespace NS
{
    namespace UI
    {
        void ColorRect::OnDraw(Gfx::Renderer& renderer, const WidgetRect& rectPx, float alpha)
        {
            const float effective = m_color.A() * alpha;
            if (effective <= 0.0f)
            {
                return;
            }

            renderer.DrawScreenRect(rectPx.x,
                                    rectPx.y,
                                    rectPx.width,
                                    rectPx.height,
                                    Color{m_color.R(), m_color.G(), m_color.B(), effective});
        }
    } // namespace UI
} // namespace NS
