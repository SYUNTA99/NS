#include "NSlib/CoreObject/UIActor.h"

#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Graphics/RenderContext.h"

namespace NS
{
    UIActor::~UIActor() noexcept
    {
        Close();
    }

    void UIActor::OnRenderOverlay(const NS::Gfx::RenderContext& context)
    {
        if (context.renderer == nullptr || m_scene == nullptr)
        {
            return;
        }
        m_widgets.Draw(context.renderer, m_scene->UI().Canvas());
    }

    void UIActor::Open(Scene& scene)
    {
        if (IsOpen())
        {
            return;
        }
        m_scene = &scene;
        scene.UI().Add(*this);
    }

    void UIActor::Close() noexcept
    {
        if (m_scene == nullptr)
        {
            return;
        }
        m_scene->UI().Remove(*this);
        m_scene = nullptr;
    }
} // namespace NS
