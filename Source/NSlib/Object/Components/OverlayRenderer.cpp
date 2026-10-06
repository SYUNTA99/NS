#include "NSlib/Object/Components/OverlayRenderer.h"

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Scene/Scene.h"

namespace NS::Obj
{
    void OverlayRenderer::OnStart()
    {
        Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->RegisterOverlay(this);
    }

    void OverlayRenderer::OnEndPlay()
    {
        Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->UnregisterOverlay(this);
    }
} // namespace NS::Obj
