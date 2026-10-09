#include "NSlib/Object/Actors/Light.h"

#include "NSlib/Object/SubObjects/DirectionalLight.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

namespace NS::Obj
{
    Light::Light() noexcept
    {
        AttachFixedSubObject(m_light);
    }

    void Light::ForEachSubObj(const SubObjVisitor& visitor) const
    {
        Actor::ForEachSubObj(visitor);
        visitor("DirectionalLight", m_light);
    }

    NS_PLACEABLE(Light, "ライト")
} // namespace NS::Obj
