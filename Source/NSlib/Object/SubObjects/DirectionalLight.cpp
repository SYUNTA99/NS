#include "NSlib/Object/SubObjects/DirectionalLight.h"

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

namespace NS::Obj
{
    NS_CLASS(DirectionalLight)

    NS::Vector3 DirectionalLight::Direction() const noexcept
    {
        NS::Vector3 direction = NS::Vector3::TransformNormal(NS::Vector3::UnitZ, RootTransform().WorldMatrix());
        direction.Normalize();
        return direction;
    }
} // namespace NS::Obj
