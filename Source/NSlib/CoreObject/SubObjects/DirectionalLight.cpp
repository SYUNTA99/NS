#include "NSlib/CoreObject/SubObjects/DirectionalLight.h"

#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    NS_CLASS(DirectionalLight)

    NS::Vector3 DirectionalLight::Direction() const noexcept
    {
        NS::Vector3 direction = NS::Vector3::TransformNormal(NS::Vector3::UnitZ, RootTransform().WorldMatrix());
        direction.Normalize();
        return direction;
    }
} // namespace NS
