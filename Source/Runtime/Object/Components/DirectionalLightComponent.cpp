#include "Runtime/Object/Components/DirectionalLightComponent.h"

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Object
{
    NS_CLASS(DirectionalLightComponent)

    void DirectionalLightComponent::OnStart()
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
        scene->RegisterLight(this);
    }

    void DirectionalLightComponent::OnEndPlay()
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
        scene->UnregisterLight(this);
    }
} // namespace NS::Object
