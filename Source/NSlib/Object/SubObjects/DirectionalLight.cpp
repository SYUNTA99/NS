#include "NSlib/Object/SubObjects/DirectionalLight.h"

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/Scene.h"

namespace NS::Obj
{
    NS_CLASS(DirectionalLight)

    void DirectionalLight::OnStart()
    {
        Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->RegisterLight(this);
    }

    void DirectionalLight::OnEndPlay()
    {
        Scene* scene = OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        scene->UnregisterLight(this);
    }
} // namespace NS::Obj
