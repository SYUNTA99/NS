#include "NSlib/Object/Actors/Light.h"

#include "NSlib/Object/Components/DirectionalLight.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"

namespace NS::Obj
{
    Light::Light() noexcept
    {
        AttachFixedComponent(m_light);
    }

    void Light::ForEachPart(const PartVisitor& visitor) const
    {
        Actor::ForEachPart(visitor);
        visitor("DirectionalLight", m_light);
    }

    NS_PLACEABLE(Light, "ライト")
} // namespace NS::Obj
