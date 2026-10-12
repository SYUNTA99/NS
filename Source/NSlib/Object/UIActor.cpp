#include "NSlib/Object/UIActor.h"

#include "NSlib/Graphics/RenderContext.h"
#include "NSlib/Object/Scene/Scene.h"

namespace NS::Obj
{
    UIActor::~UIActor() noexcept
    {
        Close();
    }

    void UIActor::OnRenderOverlay(const NS::Gfx::RenderContext& context)
    {
        if (context.renderer != nullptr)
        {
            m_widgets.Render(*context.renderer);
        }
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
} // namespace NS::Obj
