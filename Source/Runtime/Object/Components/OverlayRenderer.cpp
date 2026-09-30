#include "Runtime/Object/Components/OverlayRenderer.h"

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Obj
{
    void OverlayRenderer::OnStart()
    {
        Actor* owner = Owner();
        if (owner == nullptr)
        {
            return;
        }
        Scene* scene = owner->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->RegisterOverlay(this);
    }

    void OverlayRenderer::OnEndPlay()
    {
        Actor* owner = Owner();
        if (owner == nullptr)
        {
            return;
        }
        Scene* scene = owner->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->UnregisterOverlay(this);
    }
} // namespace NS::Obj
