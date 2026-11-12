#include "NSlib/CoreObject/Actors/LightActor.h"

#include "NSlib/CoreObject/ClassObjs/DirectionalLight.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"

namespace NS
{
    void LightActor::OnInit()
    {
        CreateClassObj(m_light);
        Vector3 initialDirection{-0.3f, -1.0f, -0.2f};
        initialDirection.Normalize();
        TransformObj().SetRotation(Quaternion::FromToRotation(Vector3::UnitZ, initialDirection));
    }

    NS_PLACEABLE(LightActor, "ライト")
} // namespace NS
