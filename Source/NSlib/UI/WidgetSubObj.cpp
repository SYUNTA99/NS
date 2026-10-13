#include "NSlib/UI/WidgetSubObj.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Graphics/Renderer.h"

namespace NS::UI
{

    WidgetSubObj::WidgetSubObj()
    {
        m_root.SetStretch(true);
    }

    void WidgetSubObj::Layout(const UICanvas& canvas) noexcept
    {
        m_root.Layout(canvas.rect);
    }

    void WidgetSubObj::Draw(NS::Gfx::Renderer* renderer, const UICanvas& canvas)
    {
        NS_ASSERT(UI, renderer != nullptr, "null の renderer で画面の木を描こうとしている");
        if (!m_root.HasChildren())
        {
            return;
        }

        Layout(canvas);
        m_root.Draw(*renderer, 1.0f, canvas.scale);
    }

    bool WidgetSubObj::ConsumesPointer(const UICanvas& canvas, float px, float py) const noexcept
    {
        if (canvas.scale <= 0.0f)
        {
            return false;
        }
        return m_root.HitTest(px / canvas.scale, py / canvas.scale);
    }

} // namespace NS::UI
