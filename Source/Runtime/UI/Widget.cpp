#include "Runtime/UI/Widget.h"

namespace NS::UI
{

    void Widget::Adopt(std::unique_ptr<Widget> child)
    {
        if (child == nullptr)
            return;
        m_children.push_back(std::move(child));
    }

    WidgetRect Widget::ResolveRect(const WidgetRect& parent) const noexcept
    {
        if (m_stretch)
            return parent;

        WidgetRect rect{};
        rect.x = parent.x + parent.width * m_anchor.x + m_offset.x - m_size.x * m_pivot.x;
        rect.y = parent.y + parent.height * m_anchor.y + m_offset.y - m_size.y * m_pivot.y;
        rect.width = m_size.x;
        rect.height = m_size.y;
        return rect;
    }

    void Widget::Layout(const WidgetRect& parent) noexcept
    {
        m_layoutRect = ResolveRect(parent);
        for (const std::unique_ptr<Widget>& child : m_children)
            child->Layout(m_layoutRect);
    }

    void Widget::Draw(NS::Graphics::Renderer& renderer, float parentAlpha, float scale)
    {
        if (!m_visible)
            return;

        const float alpha = parentAlpha * m_alpha;
        WidgetRect rectPx{};
        rectPx.x = m_layoutRect.x * scale;
        rectPx.y = m_layoutRect.y * scale;
        rectPx.width = m_layoutRect.width * scale;
        rectPx.height = m_layoutRect.height * scale;
        OnDraw(renderer, rectPx, alpha);

        for (const std::unique_ptr<Widget>& child : m_children)
            child->Draw(renderer, alpha, scale);
    }

    bool Widget::HitTest(float px, float py) const noexcept
    {
        if (!m_visible)
            return false;

        // 上に描かれる物から先に当てる。子は末尾ほど上
        for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
        {
            if ((*it)->HitTest(px, py))
                return true;
        }
        return m_blocksInput && m_layoutRect.Contains(px, py);
    }

} // namespace NS::UI
