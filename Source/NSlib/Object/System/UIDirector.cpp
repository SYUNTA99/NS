#include "NSlib/Object/System/UIDirector.h"

#include "NSlib/Graphics/RenderContext.h"
#include "NSlib/Graphics/Renderer.h"
#include "NSlib/Object/UIActor.h"

#include <algorithm>

namespace NS::Obj
{
    void UIDirector::Add(UIActor& screen)
    {
        if (std::find(m_screens.begin(), m_screens.end(), &screen) != m_screens.end())
        {
            return;
        }
        m_screens.push_back(&screen);
    }

    void UIDirector::Remove(UIActor& screen) noexcept
    {
        std::erase(m_screens, &screen);
    }

    void UIDirector::OnTick()
    {
        // 画面の OnTick が開く閉じるを呼ぶので、歩の頭の並びを写して回す
        const std::vector<UIActor*> screens = m_screens;
        for (UIActor* screen : screens)
        {
            if (std::find(m_screens.begin(), m_screens.end(), screen) == m_screens.end())
            {
                continue;
            }
            screen->OnTick();
        }
    }

    std::vector<UIActor*> UIDirector::DrawOrdered() const
    {
        std::vector<UIActor*> ordered = m_screens;
        std::stable_sort(ordered.begin(), ordered.end(), [](const UIActor* a, const UIActor* b) {
            return a->DrawOrder() < b->DrawOrder();
        });
        return ordered;
    }

    void UIDirector::Draw(const NS::Gfx::RenderContext& context)
    {
        m_canvas = NS::UI::UICanvas{};
        if (context.renderer != nullptr)
        {
            const NS::Size2D size = context.renderer->Size();
            m_canvas = NS::UI::MakeUICanvas(static_cast<float>(size.width), static_cast<float>(size.height));
        }
        for (UIActor* screen : DrawOrdered())
        {
            screen->OnRenderOverlay(context);
        }
    }
} // namespace NS::Obj
