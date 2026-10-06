#include "NSlib/Object/Components/DirectionalLight.h"

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/Scene.h"

namespace NS::Obj
{
    NS_CLASS(DirectionalLight)

    void DirectionalLight::OnStart()
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
        scene->RegisterLight(this);
    }

    void DirectionalLight::OnEndPlay()
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
        scene->UnregisterLight(this);
    }
} // namespace NS::Obj
