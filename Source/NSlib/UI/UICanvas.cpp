#include "NSlib/UI/UICanvas.h"

namespace NS::UI
{
        UICanvas MakeUICanvas(float viewportWidth, float viewportHeight) noexcept
        {
            if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
            {
                return UICanvas{};
            }

            UICanvas canvas;
            canvas.scale = viewportHeight / UICanvas::k_ReferenceHeight;
            canvas.rect = WidgetRect{0.0f, 0.0f, viewportWidth / canvas.scale, UICanvas::k_ReferenceHeight};
            return canvas;
        }
} // namespace NS::UI
