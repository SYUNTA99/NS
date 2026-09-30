#include "Runtime/Object/Actors/Light.h"

#include "Runtime/Object/Components/DirectionalLight.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Obj
{
    Light::Light() noexcept
    {
        AddComponent<DirectionalLight>();
    }

    NS_PLACEABLE(Light, "ライト")
} // namespace NS::Obj
