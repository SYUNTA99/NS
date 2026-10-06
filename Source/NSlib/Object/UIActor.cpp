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
        AttachScene(&scene);
        Appear();
    }

    void UIActor::OnAppear()
    {
        if (Scene* scene = OwningScene())
        {
            scene->RegisterUIActor(this);
        }
    }

    void UIActor::OnKill() noexcept
    {
        if (Scene* scene = OwningScene())
        {
            scene->UnregisterUIActor(this);
        }
    }

    void UIActor::Close() noexcept
    {
        Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        Kill();
        AttachScene(nullptr);
    }
} // namespace NS::Obj
