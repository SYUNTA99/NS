#include "NSlib/Object/Actors/Light.h"

#include "NSlib/Object/SubObjects/DirectionalLight.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

namespace NS::Obj
{
    void Light::Init()
    {
        (void)CreateSubObj<DirectionalLight>("DirectionalLight");
    }

    NS_PLACEABLE(Light, "ライト")
} // namespace NS::Obj
