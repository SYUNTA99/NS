#include "NSlib/Object/Actors/Light.h"

#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/SubObjects/DirectionalLight.h"

namespace NS::Obj
{
    void Light::OnInit()
    {
        (void)CreateSubObj(m_light);
        NS::Vector3 initialDirection{-0.3f, -1.0f, -0.2f};
        initialDirection.Normalize();
        Root().SetRotation(NS::Quaternion::FromToRotation(NS::Vector3::UnitZ, initialDirection));
    }

    NS_PLACEABLE(Light, "ライト")
} // namespace NS::Obj
