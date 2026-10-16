#include "NSlib/CoreObject/SubObjects/DirectionalLight.h"

#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    NS_CLASS(DirectionalLight)

    Vector3 DirectionalLight::Direction() const noexcept
    {
        Vector3 direction = Vector3::TransformNormal(Vector3::UnitZ, RootTransform().WorldMatrix());
        direction.Normalize();
        return direction;
    }
} // namespace NS
