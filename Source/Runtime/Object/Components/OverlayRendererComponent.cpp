#include "Runtime/Object/Components/OverlayRendererComponent.h"

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Object
{
    void OverlayRendererComponent::OnStart()
    {
        GameObject* owner = Owner();
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

    void OverlayRendererComponent::OnEndPlay()
    {
        GameObject* owner = Owner();
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
} // namespace NS::Object
