#include "NSlib/UI/WidgetTree.h"

#include "NSlib/Core/Assert.h"

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

        void WidgetTree::Draw(Gfx::ScreenOverlay* overlay, const UICanvas& canvas)
        {
            NS_ASSERT(UI, overlay != nullptr, "null の overlay で画面の木を描こうとしている");
            if (!m_root.HasChildren())
            {
                return;
            }

            Layout(canvas);
            m_root.Draw(*overlay, 1.0f, canvas.scale);
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
