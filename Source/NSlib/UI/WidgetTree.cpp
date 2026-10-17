#include "NSlib/UI/WidgetTree.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Gfx/Renderer.h"

namespace NS
{
    namespace UI
    {
        WidgetTree::WidgetTree()
        {
            m_root.SetStretch(true);
        }

        void WidgetTree::Layout(const UICanvas& canvas) noexcept
        {
            m_root.Layout(canvas.rect);
        }

        void WidgetTree::Draw(Gfx::Renderer* renderer, const UICanvas& canvas)
        {
            NS_ASSERT(UI, renderer != nullptr, "null の renderer で画面の木を描こうとしている");
            if (!m_root.HasChildren())
            {
                return;
            }

            Layout(canvas);
            m_root.Draw(*renderer, 1.0f, canvas.scale);
        }

        bool WidgetTree::ConsumesPointer(const UICanvas& canvas, float px, float py) const noexcept
        {
            if (canvas.scale <= 0.0f)
            {
                return false;
            }
            return m_root.HitTest(px / canvas.scale, py / canvas.scale);
        }
    } // namespace UI
} // namespace NS
